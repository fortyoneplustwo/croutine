#define __USE_GNU

#include "scheduler.h"
#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "ringbuf.h"
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

static int unregisterev(int fd, uint32_t ev) {
  uint32_t evremoved = np->registered_events[fd] & ~ev;
  struct epoll_event newev = (struct epoll_event){
      .events = evremoved,
      .data = {.fd = fd},
  };
  if (np_reg(fd, &newev) == -1) {
    perror("failed to unregister event with no matching fiber");
    return -1;
  }
  np->registered_events[fd] = evremoved;
  return 0;
}

static void poll_io() {
  int timeout = !!sched->nready - 1;
  np->nready = epoll_wait(np->fd, np->events, MAX_EVENTS, timeout);

  if (np->nready == -1) {
    exit(3);
  }

  // dispatch events
  for (int i = 0; i < np->nready; i++) {
    uint32_t readyev = (&np->events[i])->events;
    uint32_t readyfd = (&np->events[i])->data.fd;

    if ((readyev & NPIN) == NPIN) {
      fiber_t *reader = iorequests[readyfd].curreader;
      if (!reader) {
        unregisterev(readyfd, readyev);
      } else {
        rb_enqueue(sched->runq, reader);
        reader->state = READY;
        sched->nready++;
      }
    }

    if ((readyev & NPOUT) == NPOUT) {
      fiber_t *writer = iorequests[readyfd].curwriter;
      if (!writer) {
        unregisterev(readyfd, readyev);
      } else {
        rb_enqueue(sched->runq, writer);
        writer->state = READY;
        sched->nready++;
      }
    }
  }
}

void sched_run() {
  while (1) {
    // poll io before dequeueing
    poll_io();

    fiber_t *next = rb_dequeue(sched->runq);
    if (!next)
      abort();
    sched->nready--;

    fiber_run(next);

    switch (next->state) {
    case YIELDED:
      next->state = READY;
      if (rb_enqueue(sched->runq, next) == -1) {
        exit(2);
      }
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

void freesched(scheduler_t *s) {
  if (!s)
    return;
  freerbuf(s->runq);
  fstack_free(s->self);
  free(s->self);
  free(s);
}

int sched_init() {
  int err;
  sched = (scheduler_t *)calloc(1, sizeof(scheduler_t));
  if (!sched) {
    return 1;
  }
  sched->runq = rbuf_init(RBUFDEFAULTCAP);
  if (!sched->runq) {
    freesched(sched);
    return -1;
  }
  np = np_init();
  if (!np) {
    freesched(sched);
    fprintf(stderr, "could not init netpoller\n");
    return 1;
  }
  for (int i = 0; i < MAX_FDS; i++) {
    iorequests[i] = (fd_waiters_t){0};
    iorequests[i].readersq = rbuf_init(RBUFDEFAULTCAP);
    iorequests[i].writersq = rbuf_init(RBUFDEFAULTCAP);
  }
  sched->self = fiber_create(sched_run, NULL, 0);
  // printf("created scheduler fiber with id %d\n", sched->self->id);
  // fiber_t *npfiber = fiber_create(np_run, NULL, -1);
  // printf("created netpoller fiber with id %d\n", npfiber->id);
  // npfiber->state = READY;
  // err = rb_enqueue(sched->runq, npfiber);
  // if (err) {
  //   // TODO: handle error
  // }
  // sched->nready++;
  return 0;
}

static void execmain(void *args) {
  void (*f)(int, char **) = ((main_t *)args)->main;
  int argc = ((main_t *)args)->argc;
  char **argv = ((main_t *)args)->argv;
  f(argc, argv);
  switch_context(&sched->running->context, &sched->self->caller);
}

int sched_start(void (*main)(int, char **), int argc, char **argv) {
  int err;
  main_t *mainfn = (main_t *)malloc(sizeof(main_t));
  if (!mainfn) {
    fprintf(stderr, "failed to allocate memory for main fn\n");
    return 1;
  }
  *mainfn = (main_t){.main = main, .argc = argc, .argv = argv};
  fiber_t *f = fiber_create(execmain, (void *)mainfn, count++);
  err = rb_prepend(sched->runq, f);
  if (err) {
    // TODO: handle error
  }
  sched->nready++;
  switch_context(&sched->self->caller, &sched->self->context);
  fstack_free(f);
  free(f);
  free(mainfn);
  return 0;
}

// void wakeall(node_t **head) {
//   while (*head) {
//     node_t *waiter_node = dequeue_node(head);
//     fiber_t *waiter_fib = (fiber_t *)waiter_node->data;
//     waiter_fib->state = READY;
//     prepend(&sched->runq, waiter_node);
//   }
// }
