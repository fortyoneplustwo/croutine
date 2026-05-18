#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STACK_SIZE 2048

extern void switch_context(context_t *, context_t *);

static int count = 0;

typedef struct {
  fiber_t *fiber;
  void *next;
} job_t;

typedef struct {
  job_t *run_q;
  fiber_t *curr;
  fiber_t *self;
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

void wc_enqueue(void *wc_ptr, fiber_t *f) {
  printf("wc_enqueue, waiting %d\n", f->id);
  job_t **p = (job_t **)wc_ptr;
  printf("wc_enqueue2\n");
  printf("what does p point to? %x\n", *p);
  if (!(*p)) {
    printf("our waitlist is empty\n");
    *p = wrap(f);
    return;
  }
  job_t *curr = *p;
  while (curr->next != NULL) {
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

fiber_t *fiber_create(void *(*entry)(), void *args, size_t len) {
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
  // Set id
  self->id = count++;

  return self;
}

fiber_t *fiber_spawn(void *(*entry)(), void *args, size_t len) {
  fiber_t *self = fiber_create(entry, args, len);
  push_to_front(self);
  printf("Spawned fiber %d\n", self->id);
  return self;
}

void fiber_run(fiber_t *f, void **result) {
  printf("Fiber %d: ", f->id);
  sched->curr = f;
  f->state = RUNNING;
  f->result = result;
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

int sched_run(void) {
  while (sched->run_q) {
    fiber_t *next = dequeue();

    fiber_run(next, NULL);
    printf("dont running fiber %d\n", next->id);

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
  // TODO: decide what should happen at the end of the loop?
  sched->curr = NULL;
  switch_context(&sched->self->context, &sched->self->caller);
  return 0;
}

int sched_init(void) {
  sched = (scheduler_t *)calloc(1, sizeof(scheduler_t));
  if (!sched) {
    return 1;
  }
  // create the scheduler fiber and push sched_run onto its stack
  // this has to happen only once, so we do this in init for now
  sched->self = fiber_create((void *)sched_run, NULL, 0);
  printf("created scheduler fiber with id %d\n", sched->self->id);
  return 0;
}

void switch_context_as_entry(void *arg) {
  context_t old;
  context_t *new = (context_t *)arg;
  sched->curr->state = DEAD;
  sched->curr = NULL;
  printf("switch entry\n");
  switch_context(&old, new);
}

void fiber_await(fiber_t *f) {
  printf("inside await\n");
  // 'We' == the caller of fiber_await()
  // =========================================================================
  // If we are a fiber
  // -------------------------------------------------------------------------
  // 1. We just add ourself to f's waitlist.
  // 2. Set our state to BLOCKED
  // 3. A fiber MUST have been called by the scheduler, so it makes sense to
  //    just switch context back to our caller.
  // 4. Return
  // =========================================================================
  // If we are NOT a fiber
  // -------------------------------------------------------------------------
  // 1. Create a fiber representation of ourself. Why? So that we don't
  //    we can keep the existing logic simple by only dealing with fibers rather
  //    than creating edge cases for non-fibers.
  //    What would this fiber look like? When picked up by the scheduler,
  //    it would
  //    a. Set it's state to DEAD so that it can be cleaned up when the
  //       scheduler resumes
  //    b. Set sched->curr to NULL because we are about to run a non-fiber.
  //       This basically ensures that the next time await is called from
  //       a non-fiber, it will find sched->curr to be NULL and will thus
  //       appropriately create a fiber-self.
  //    c. Switch context back to our non-fiber self
  //    Optimization: this stack can be super small.
  // 2. Add our fiber self to f's waitlist
  // 3. Set our fiber self's state to BLOCKED
  // 4. Switch context to the scheduler, but save our context to the arg
  //    passed to our fiber self's entry function
  // 5. Return
  // =========================================================================
  fiber_t *self = sched->curr;
  if (!self) {
    context_t initiator_ctx;
    self = fiber_create((void *)switch_context_as_entry, (void *)&initiator_ctx,
                        1);
    printf("created fiber self %d\n", self->id);
    wc_enqueue(&f->waitlist, self);
    self->state = BLOCKED;
    switch_context(&initiator_ctx, &sched->self->context);
    printf("returning from await\n");
    fiber_destroy(self);
    return;
  }
  printf("we are already a fiber\n");
  printf("supposed to wait for fiber %d\n", f->id);
  printf("we are fiber %d\n", self->id);
  wc_enqueue(&f->waitlist, self);
  printf("about to set %d's state to blocked\n", self->id);
  self->state = BLOCKED;
  printf("was able to set %d's state to blocked\n", self->id);
  switch_context(&self->context, &sched->self->context);
  return;
}
