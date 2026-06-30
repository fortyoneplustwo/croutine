#define __USE_GNU

#include "scheduler.h"
#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include <bits/types/siginfo_t.h>
#include <bits/types/sigset_t.h>
#include <bits/types/stack_t.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/ucontext.h>

scheduler_t *sched;

typedef struct {
  void (*main)(int, char **);
  int argc;
  char **argv;
} main_t;

static int iodispatch() {
  if (np->nready == -1)
    return -1;

  int err = 0;
  uint32_t readyev, readyfd;
  node_t *curwaiter;

  for (int i = 0; i < np->nready; i++) {
    readyev = (&np->events[i])->events;
    readyfd = (&np->events[i])->data.fd;
    curwaiter = iorequests[readyfd].waitq;

    while (curwaiter) {
      fiber_t *f = (fiber_t *)curwaiter->data;
      if (readyfd == f->expectev.fd &&
          ((readyev & f->expectev.event) == f->expectev.event)) {
        break;
      }
      curwaiter = curwaiter->next;
    }

    if (curwaiter) {
      fiber_t *f = (fiber_t *)curwaiter->data;
      f->state = READY;
      enqueue(&sched->runq, f);
      sched->nready++;
    } else {
      uint32_t readyevremoved = np->registered_events[readyfd] & ~readyev;
      struct epoll_event ev = (struct epoll_event){
          .events = readyevremoved,
          .data = {.fd = readyfd},
      };
      if (epoll_ctl(np->fd, EPOLL_CTL_MOD, readyfd, &ev) == -1) {
        perror("failed to unregister event with no matching fiber");
        err = -1;
      }
      np->registered_events[readyfd] = readyevremoved;
    }
  }
  return err;
}

void sched_run() {
  while (sched->runq) {
    fiber_t *next = dequeue(&sched->runq);
    sched->nready--;

    fiber_run(next);

  dispatch_io:
    if (next->id == -1) {
      int err = iodispatch();
      if (err) {
        fprintf(stderr, "failed to dispatch io");
      }
      while (sched->nready == 0) {
        sigset_t set;
        sigemptyset(&set);
        printf("going to sleep waiting for I/O\n");
        int nready = epoll_pwait(np->fd, np->events, MAX_EVENTS, -1, &set);
        if (nready == -1) {
          perror("epoll_pwait failed");
        }
        if (nready > 0) {
          np->nready = nready;
          goto dispatch_io;
        }
      }
    }

    switch (next->state) {
    case YIELDED:
      next->state = READY;
      enqueue(&sched->runq, next);
      sched->nready++;
      continue;
    case DEAD:
      fstack_free(next);
      free(next);
      continue;
    case BLOCKED:
      continue;
    case RUNNING:
    case READY:
      printf("this should not happen!\n");
      abort();
    default:
      continue;
    }
  }
}

int sched_init() {
  sched = (scheduler_t *)calloc(1, sizeof(scheduler_t));
  if (!sched) {
    return 1;
  }
  np = np_init();
  if (!np) {
    fprintf(stderr, "could not init netpoller\n");
    return 1;
  }
  for (int i = 0; i < MAX_FDS; i++) {
    iorequests[i] = (ioreq_t){0};
  }
  sched->self = fiber_create(sched_run, NULL, 0);
  printf("created scheduler fiber with id %d\n", sched->self->id);
  fiber_t *npfiber = fiber_create(np_run, NULL, -1);
  printf("created netpoller fiber with id %d\n", npfiber->id);
  npfiber->state = READY;
  enqueue(&sched->runq, npfiber);
  sched->nready++;
  return 0;
}

static void execmain(void *args) {
  void (*f)(int, char **) = ((main_t *)args)->main;
  int argc = ((main_t *)args)->argc;
  char **argv = ((main_t *)args)->argv;
  f(argc, argv);
  switch_context(&sched->running->context, &sched->self->caller);
}

int sched_start(void (*main)(int, char**), int argc, char **argv) {
  main_t *mainfn = (main_t *)malloc(sizeof(main_t));
  if (!mainfn) {
    fprintf(stderr, "failed to allocate memory for main fn\n");
    return 1;
  }
  *mainfn = (main_t){.main = main, .argc = argc, .argv = argv};
  fiber_t *f = fiber_create(execmain, (void *)mainfn, count++);
  push_front(&sched->runq, f);
  sched->nready++;
  switch_context(&sched->self->caller, &sched->self->context);
  fstack_free(f);
  free(f);
  free(mainfn);
  return 0;
}

void wakeall(node_t **head) {
  while (*head) {
    node_t *waiter_node = dequeue_node(head);
    fiber_t *waiter_fib = (fiber_t *)waiter_node->data;
    waiter_fib->state = READY;
    prepend(&sched->runq, waiter_node);
  }
}
