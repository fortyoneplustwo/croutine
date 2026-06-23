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

void sched_run(void) {
  while (sched->run_q) {
    fiber_t *next = dequeue((node_t **)&sched->run_q);
    sched->nfibers--;

    fiber_run(next);

  dispatch_io:
    if (next->id == np->fid && np->nready != -1) {
      for (int i = 0; i < np->nready; i++) {
        struct epoll_event *ready = &np->events[i];
        node_t *cur = ioreqs[ready->data.fd].waitq;
        fiber_t *f = NULL;
        while (cur) {
          f = (fiber_t *)cur->data;
          if ((ready->events & f->events) == f->events) {
            break;
          }
          cur = cur->next;
        }
        if (!cur) {
          int err;
          struct epoll_event ev;
          uint32_t curremoved = np->fdregistry[ready->data.fd] & ~ready->events;
          ev.events = curremoved;
          ev.data.fd = ready->data.fd;
          err = epoll_ctl(np->fd, EPOLL_CTL_MOD, ready->data.fd, &ev);
          if (err) {
            perror("failed to unregister event with no matching fiber");
          } else {
            np->fdregistry[ready->data.fd] = ev.events;
          }
          continue;
        }
        f->state = READY;
        enqueue((node_t **)&sched->run_q, f);
        sched->nfibers++;
      }
    }

    // If there are no fibers on the run queue,
    // put the thread to sleep until an event is ready.
    while (sched->nfibers == 0) {
      sigset_t set;
      sigemptyset(&set);
      int n = epoll_pwait(np->fd, np->events, MAX_EVENTS, -1, &set);
      if (n == -1) {
        perror("epoll_pwait failed");
      }
      if (n == 0) {
        continue;
      }
      np->nready = n;
      goto dispatch_io;
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

int sched_init(void) {
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
    ioreqs[i] = (ioreq_t){0};
  }
  // Create the dedicated scheduler fiber with id 0
  sched->self = fiber_create(sched_run, NULL, 0);
  printf("created scheduler fiber with id %d\n", sched->self->id);
  // Create the dedicated netpoller fiber with id -1
  // and push it onto the jobs queue.
  fiber_t *npfiber = fiber_create(np_run, NULL, -1);
  printf("created netpoller fiber with id %d\n", npfiber->id);
  npfiber->state = READY;
  enqueue((node_t **)&sched->run_q, npfiber);
  sched->nfibers++;
  return 0;
}

void exec_and_switch_ctx(void (*f)()) {
  // ((void (*)())f)();
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
