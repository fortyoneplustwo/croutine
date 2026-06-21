#include "scheduler.h"
#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

scheduler_t *sched;

void sched_run(void) {
  while (sched->run_q) {
    // TODO: at some point need to poll I/O before every deque
    fiber_t *next = dequeue((node_t **)&sched->run_q);

    fiber_run(next);

    // If we have just run the netpoller fiber,
    // then enqueue any fibers ready for io.
    if (next->id == np->fid) {
      if (np->nready == -1) {
        // handle error
      } else {
        // For each ready fd, get the first matching fiber
        // waiting on it
        for (int i = 0; i < np->nready; i++) {
          struct epoll_event *want = &np->events[i];
          node_t *cur = ioreqs[want->data.fd].waitq;
          fiber_t *f = NULL;
          while (cur) {
            f = (fiber_t *)cur->data;
            if (f->events == want->events) {
              break;
            }
            cur = cur->next;
          }
          if (!cur) {
            continue;
          }
          f->state = READY;
          enqueue((node_t **)&sched->run_q, f);
        }
      }
    }

    switch (next->state) {
    case YIELDED:
      next->state = READY;
      enqueue((node_t **)&sched->run_q, next);
      continue;
    case DEAD:
      fstack_free(next);
      free(next);
      continue;
    default:
      continue;
    }
  }
  // TODO: Decide what should happen here.
  // When do we actually break from the loop?
  // Idea:
  //  Poll for I/O (level-triggered) at each iteration.
  //  When there are no fibers on the run_q,
  //  break out of the loop and swtich to edge-triggered epoll
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
  // Create the dedicated scheduler fiber with id=0
  sched->self = fiber_create((void *)sched_run, NULL, 0);
  printf("created scheduler fiber with id %d\n", sched->self->id);
  // Create the dedicated netpoller fiber with id=-1
  // and push it onto the jobs queue.
  fiber_t *npfiber = fiber_create((void *)np_run, NULL, -1);
  printf("created netpoller fiber with id %d\n", npfiber->id);
  npfiber->state = READY;
  enqueue((node_t **)&sched->run_q, npfiber);
  return 0;
}

void exec_and_switch_ctx(void *f) {
  ((void (*)())f)();
  switch_context(&sched->curr->context, &sched->self->caller);
}

int sched_start(void (*main)()) {
  fiber_t *f = fiber_create((void *)exec_and_switch_ctx, (void *)main, count++);
  push_front((node_t **)&sched->run_q, f);
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
