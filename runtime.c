#include "runtime.h"
#include "netpoller.h"
#include "queue.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#define STACK_SIZE 2048

extern void switch_context(context_t *, context_t *);

static int count = 1;

typedef struct {
  fiber_t *fiber;
  void *next;
} job_t;

typedef struct {
  void *run_q;
  fiber_t *curr;
  fiber_t *self;
  int netpollfd;
} scheduler_t;

typedef struct {
  fiber_t *curreader;
  fiber_t *curwriter;
  node_t *waitq;
} ioreq_t;

// global scheduler
scheduler_t *sched;
// global netpoller
netpoller_t *np;
// global io queue
ioreq_t ioreqs[MAX_FDS];

// Destroy the fiber's stack
void fstack_free(fiber_t *f) {
  printf("Destroying fiber %d's stack\n", f->id);
  if (f->stack) {
    free(f->stack);
    f->stack = NULL;
  }
}

static void fiber_trampoline(fiber_t *f) {
  void *result = f->entry(f->args);
  if (f->result) {
    *(f->result) = result;
  }
  printf("Job done. Switching back to scheduler...\n");
  f->state = DEAD;
  switch_context(&f->context, &sched->self->context);
}

fiber_t *fiber_create(void *(*entry)(), void *args, size_t len, void **result,
                      int id) {
  fiber_t *self = calloc(1, sizeof(fiber_t));
  if (!self) {
    fprintf(stderr, "Couldn't allocate memory for new fiber context\n");
    return NULL;
  }

  // Create new stack, 16 bytes aligned
  void *stack = NULL;
  int status = posix_memalign(&stack, 16, STACK_SIZE * sizeof(uint64_t));
  if (status != 0) {
    fprintf(stderr, "Error allocating mem for stack: %d\n", status);
    return NULL;
  }
  self->stack = stack;
  // Stack grows downward, so must point to the end of block
  stack = (uint64_t *)stack + STACK_SIZE;
  // Add padding for the Red Zone
  stack = (uint64_t *)stack - 128;
  // Push trampoline onto the stack
  stack = (uint64_t *)stack - 1;
  *(uint64_t *)stack = (uint64_t)fiber_trampoline;

  // Set argument of trampoline (rdi)
  self->context.rdi = (uint64_t)self;
  // Set stack pointer (rsp)
  self->context.rsp = (uint64_t)stack;
  // Set entry function
  self->entry = entry;
  // Set args of entry function
  self->args = args;
  self->len = len; // NOTE: isn't this redundant?
  // Set result
  self->result = result;
  // Set id
  self->id = id++;

  return self;
}

fiber_t *fiber_spawn(void *(*entry)(), void *args, size_t len, void **result) {
  fiber_t *self = fiber_create(entry, args, len, result, count++);
  push_front((node_t **)&sched->run_q, self);
  printf("Spawned fiber %d\n", self->id);
  return self;
}

void fiber_run(fiber_t *f) {
  printf("Fiber %d: ", f->id);
  sched->curr = f;
  f->state = RUNNING;
  switch_context(&sched->self->context, &f->context);
}

void fiber_yield() {
  sched->curr->state = YIELDED;
  switch_context(&sched->curr->context, &sched->self->context);
}

void wakeall(fiber_t *f) {
  if (!f->waitlist) {
    return;
  }
  node_t **waitlist = (node_t **)&f->waitlist;
  while (*waitlist) {
    node_t *node = dequeue_node(waitlist);
    fiber_t *blocked = (fiber_t *)node->data;
    blocked->state = READY;
    node_t *n = (node_t *)sched->run_q;
    fiber_t *f = (fiber_t *)n->data;
    prepend((node_t **)&sched->run_q, node);
    n = (node_t *)sched->run_q;
    f = (fiber_t *)n->data;
  }
}

int sched_run(void) {
  while (sched->run_q) {
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
      wakeall(next);
      continue;
    default:
      continue;
    }
  }
  // TODO:
  // Decide what should happen at the end of the loop.
  // Just switch back to caller ctx from now.
  sched->curr = NULL;
  switch_context(&sched->self->context, &sched->self->caller);
  return 0;
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
  sched->self = fiber_create((void *)sched_run, NULL, 0, NULL, 0);
  printf("created scheduler fiber with id %d\n", sched->self->id);
  // Create the dedicated netpoller fiber with id=-1
  // and push it onto the jobs queue.
  fiber_t *npfiber = fiber_create((void *)np_run, NULL, 0, NULL, -1);
  printf("created netpoller fiber with id %d\n", npfiber->id);
  npfiber->state = READY;
  enqueue((node_t **)&sched->run_q, npfiber);
  return 0;
}

void switch_context_as_entry(void *arg) {
  context_t old;
  context_t *new = (context_t *)arg;
  sched->curr->state = DEAD;
  sched->curr = NULL;
  switch_context(&old, new);
}

void fiber_await(fiber_t *f) {
  if (f->state == DEAD) {
    fstack_free(f);
    return;
  }
  fiber_t *self = sched->curr;
  if (!self) {
    self = fiber_create((void *)switch_context_as_entry,
                        (void *)&sched->self->caller, 1, NULL, count++);
    enqueue((node_t **)&f->waitlist, self);
    self->state = BLOCKED;
    switch_context(&sched->self->caller, &sched->self->context);
    fstack_free(self);
    return;
  }
  enqueue((node_t **)&f->waitlist, self);
  self->state = BLOCKED;
  switch_context(&self->context, &sched->self->context);
  return;
}

void ioq_remove(node_t **head, fiber_t *f) {
  node_t *cur = *head;
  node_t *prev = NULL;
  fiber_t *data = NULL;
  while (cur) {
    data = (fiber_t *)cur->data;
    if (data == f) {
      if (cur == *head) {
        *head = cur->next;
      } else {
        prev->next = cur->next;
      }
      free(cur);
      return;
    }
    prev = cur;
    cur = cur->next;
  }
}

// Each fiber can only wait on one fd at a time
ssize_t fiber_read(int fd, void *buf, size_t count) {
  struct epoll_event ev =
      (struct epoll_event){.events = EPOLLIN, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }

  sched->curr->events = ev.events;
  // Try read first because the fd might be ready
  // If not then it will return EAGAIN or EWOULDBLOCK
  // in which case we can queue up for I/O
  // but what if we are already queued from the previous call?
  // need a way to signal that we already own the fd
  // and thus no need to queue up again
  // we need each fd to keep track of:
  // - its current owner,
  // - its write queue,
  // - its read queue,
  while (1) {
    ssize_t n = read(fd, buf, count);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      printf("nothing to read yet; queuing up\n");
      // not ready, queue up if not already owner
      if (ioreqs[fd].curreader != sched->curr) {
        enqueue(&ioreqs[fd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      continue;
    }
    if (n > 0) {
      ioreqs[fd].curreader = sched->curr;
    }
    if (n == 0) {
      ioreqs[fd].curreader = NULL;
    }
    // If error or buffer is drained, then we are done (for now)
    if (n <= 0) {
      ioq_remove(&ioreqs[fd].waitq, sched->curr);
      sched->curr->events = 0;
    }
    return n;
  }
}

ssize_t fiber_write(int fd, void *buf, size_t count) {
  struct epoll_event ev =
      (struct epoll_event){.events = EPOLLOUT, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->curr->events = ev.events;
  while (1) {
    ssize_t n = write(fd, buf, count);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (ioreqs[fd].curwriter != sched->curr) {
        enqueue(&ioreqs[fd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      continue;
    }
    if (n > 0) {
      ioreqs[fd].curwriter = sched->curr;
    }
    if (n == 0) {
      ioreqs[fd].curwriter = NULL;
    }
    if (n <= 0) {
      ioq_remove(&ioreqs[fd].waitq, sched->curr);
      sched->curr->events = 0;
    }
    return n;
  }
}
