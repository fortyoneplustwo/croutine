#include "runtime.h"
#include "netpoller.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
  job_t *run_q;
  fiber_t *curr;
  fiber_t *self;
  int netpollfd;
} scheduler_t;

// global scheduler
scheduler_t *sched;
// global netpoller
netpoller_t *np;
// global io queue
job_t *fd_q[MAX_FDS];

job_t *wrap(fiber_t *content) {
  job_t *wrapper = (job_t *)calloc(1, sizeof(job_t));
  if (wrapper == NULL) {
    return NULL;
  }
  wrapper->fiber = content;
  return wrapper;
}

fiber_t *unwrap(job_t *wrapped) {
  fiber_t *inside = wrapped->fiber;
  free(wrapped);
  return inside;
}

void wc_enqueue(void *wc_ptr, fiber_t *f) {
  job_t **p = (job_t **)wc_ptr;
  if (!(*p)) {
    *p = wrap(f);
    return;
  }
  job_t *curr = *p;
  while (curr->next) {
    curr = curr->next;
  }
  f->state = READY;
  job_t *new_job = wrap(f);
  curr->next = new_job;
  return;
}

int enqueue(fiber_t *f) {
  if (sched->run_q == NULL) {
    sched->run_q = wrap(f);
    return 0;
  }
  job_t *curr = sched->run_q;
  while (curr->next != NULL) {
    curr = curr->next;
  }
  f->state = READY;
  job_t *new_job = wrap(f);
  curr->next = new_job;
  return 0;
}

int push_to_front(fiber_t *f) {
  job_t *head = sched->run_q;
  job_t *job = wrap(f);
  if (job == NULL) {
    return 1;
  }
  job->next = head;
  sched->run_q = job;
  return 0;
}

static fiber_t *dequeue() {
  if (!sched->run_q) {
    return NULL;
  }
  job_t *first = sched->run_q;
  sched->run_q = first->next;
  fiber_t *fib = unwrap(first);
  return fib;
}

// Destroy the fiber's stack
void fiber_destroy(fiber_t *f) {
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
  fiber_t *self = fiber_create(entry, args, len, result, count);
  push_to_front(self);
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

// TODO: rewrite this
void wake_all(fiber_t *f) {
  job_t *curr = (job_t *)f->waitlist;
  if (!curr) {
    return;
  }
  while (curr) {
    job_t *next = curr->next;
    fiber_t *f = unwrap(curr);
    push_to_front(f);
    curr = next;
  }
}

static int fd_enqueue(int fd, fiber_t *f) {
  job_t **q = &fd_q[fd];
  if (*q == NULL) {
    *q = wrap(f);
    return 0;
  }
  job_t *curr = *q;
  while (curr->next != NULL) {
    curr = curr->next;
  }
  f->state = READY;
  job_t *new_job = wrap(f);
  curr->next = new_job;
  return 0;
}

static fiber_t *fd_unsub(int fd, int fid) {
  job_t **q = &fd_q[fd];
  if (!(*q)) {
    return NULL;
  }
  if ((*q)->fiber->id == fid) {
    job_t *first = *q;
    *q = (*q)->next;
    return unwrap(first);
  }
  job_t *cur = *q;
  while (cur->next) {
    job_t *next = cur->next;
    if (next->fiber->id == fid) {
      cur->next = next->next;
      return unwrap(next);
    }
    cur = cur->next;
  }
  return NULL;
}

ssize_t fiber_read(int fd, void *buf, size_t count) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }

  fiber_t *self = sched->curr;
  self->events = ev.events;
  fd_enqueue(fd, self);

  fiber_yield();

  // should not block since it is ready
  int n = read(fd, buf, count);
  if (n == -1) {
    return -1;
  }

  fiber_t *f = fd_unsub(fd, self->id);
  if (!f) {
    printf("could not unsub fiber. this shouldn't happen\n");
  }
  return n;
}

int sched_run(void) {
  while (sched->run_q) {
    fiber_t *next = dequeue();

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
          struct epoll_event *ev = &np->events[i];
          job_t *cur = fd_q[ev->data.fd];
          while (cur) {
            if (cur->fiber->events == ev->events) {
              break;
            }
            cur = cur->next;
          }
          if (!cur) {
            continue;
          }
          // Enqueue, but let fread() remove from fd_q
          // when it is finished performing io.
          enqueue(cur->fiber);
        }
      }
    }

    switch (next->state) {
    case YIELDED:
      enqueue(next);
      continue;
    case DEAD:
      wake_all(next);
      fiber_destroy(next);
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
  // Create the dedicated scheduler fiber with id=0
  sched->self = fiber_create((void *)sched_run, NULL, 0, NULL, 0);
  printf("created scheduler fiber with id %d\n", sched->self->id);
  // Create the dedicated netpoller fiber with id=-1
  // and push it onto the jobs queue.
  fiber_t *netpoller = fiber_create((void *)np_run, NULL, 0, NULL, -1);
  enqueue(netpoller);
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
  fiber_t *self = sched->curr;
  if (!self) {
    self = fiber_create((void *)switch_context_as_entry,
                        (void *)&sched->self->caller, 1, NULL, count);
    wc_enqueue(&f->waitlist, self);
    self->state = BLOCKED;
    switch_context(&sched->self->caller, &sched->self->context);
    fiber_destroy(self);
    return;
  }
  wc_enqueue(&f->waitlist, self);
  self->state = BLOCKED;
  switch_context(&self->context, &sched->self->context);
  return;
}
