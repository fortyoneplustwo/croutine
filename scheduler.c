#include "scheduler.h"
#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

scheduler_t *sched;

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
      enqueue((node_t **)&sched->run_q, f);
      sched->nfibers++;
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
  while (sched->run_q) {
    fiber_t *next = dequeue((node_t **)&sched->run_q);
    sched->nfibers--;

    fiber_run(next);

  dispatch_io:
    if (next->id == -1) {
      int err = iodispatch();
      if (err) {
        fprintf(stderr, "failed to dispatch io");
      }
      while (sched->nfibers == 0) {
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
      enqueue((node_t **)&sched->run_q, next);
      sched->nfibers++;
      continue;
    case DEAD:
      fstack_free(next);
      free(next);
      continue;
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
  enqueue((node_t **)&sched->run_q, npfiber);
  sched->nfibers++;
  return 0;
}

void exec_and_switch_ctx(void (*f)()) {
  f();
  switch_context(&sched->curr->context, &sched->self->caller);
}

int sched_start(void (*main)()) {
  fiber_t *f = fiber_create(exec_and_switch_ctx, (void *)main, count++);
  push_front((node_t **)&sched->run_q, f);
  sched->nfibers++;
  switch_context(&sched->self->caller, &sched->self->context);
  fstack_free(f);
  free(f);
  return 0;
}

void wakeall(node_t **head) {
  while (*head) {
    node_t *waiter_node = dequeue_node(head);
    fiber_t *waiter_fib = (fiber_t *)waiter_node->data;
    waiter_fib->state = READY;
    prepend((node_t **)&sched->run_q, waiter_node);
  }
}
