#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

#define STACK_SIZE 256

extern void switch_context(context_t *, context_t *);

static int count = 0;

typedef struct {
  fiber_t *fiber;
  void *next;
} job_t;

typedef struct {
  job_t *run_q;
  fiber_t *curr;
  fiber_t *awaiting;
} scheduler_t;

// global scheduler
scheduler_t *sched;

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

void fiber_destroy(fiber_t *f) {
  printf("Destroying fiber %d\n", f->id);
  if (f->stack) {
    free(f->stack);
  }
}

static void fiber_trampoline(fiber_t *f) {
  void *result = f->entry(f->args);
  if (f->result) {
    *(f->result) = result;
  }
  printf("Job done. Switching back to scheduler...\n");
  f->state = DEAD;
  switch_context(&f->context, &f->caller);
}

fiber_t *fiber_spawn(void *(*entry)(), void *args, size_t len) {
  fiber_t *self = malloc(sizeof(fiber_t));
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
  // Set id
  self->id = count++;
  push_to_front(self);
  printf("Spawned fiber %d\n", self->id);
  return self;
}

void fiber_run(fiber_t *f, void **result) {
  printf("Fiber %d: ", f->id);
  f->state = RUNNING;
  f->result = result;
  switch_context(&f->caller, &f->context);
}

void fiber_yield() {
  sched->curr->state = YIELDED;
  // printf("Yielding\n");
  switch_context(&sched->curr->context, &sched->curr->caller);
}

int sched_run(void) {
  while (sched->run_q) {
    sched->curr = dequeue();

    fiber_run(sched->curr, NULL);

    switch (sched->curr->state) {
    case YIELDED:
      enqueue(sched->curr);
      continue;
    case DEAD:
      // must awake every fiber in its wait list
      // but since only main can call await
      // we just check if this fiber was being awaited
      // and return control back to main
      if (sched->awaiting == sched->curr) {
        return 0;
      }
    default:
      break;
    }

    fiber_destroy(sched->curr);
    sched->curr = NULL;
  }
  sched->awaiting = NULL;

  return 0;
}

int sched_init(void) {
  sched = (scheduler_t *)calloc(1, sizeof(scheduler_t));
  if (!sched) {
    return 1;
  }
  return 0;
}

// this should basically just yield control to the scheduler
// which in turn resumes the jobs loop
void fiber_await(fiber_t *f) {
  // technically we should be adding ourself to the f's wait list
  // but since only the main thread can call await for now,
  // we can just keep track of which fiber it is waiting for
  sched->awaiting = f;
  sched_run();
  return;
}
