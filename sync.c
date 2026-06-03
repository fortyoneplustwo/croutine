#include "sync.h"
#include "fiber.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

// Returns a new waitgroup as an rvalue.
// A waitgroup is a counting semaphore.
waitgroup_t wg_make() { return (waitgroup_t){.count = 0}; }

// Increments waitgroup's count by n.
// If the count goes negative, wg_add panics.
void wg_add(waitgroup_t *wg, int n) {
  wg->count += n;
  if (wg->count < 0) {
    fprintf(stderr, "panic: wg_add: waitgroup count cannot be negative\n");
    abort();
  }
}

// Decrements the waitgroup's count.
// If count reaches 0, all waiting tasks are scheduled to run.
void wg_done(waitgroup_t *wg) {
  wg->count -= 1;
  if (wg->count == 0) {
    wakeall(&wg->wait_q);
  }
}

struct task {
  void *(*fn)(void *);
  void *args;
  waitgroup_t *wg;
};

// Wraps the function arg `f` in a wrapper function that synchronously
// 1. executes `fn(args)`
// 2. decrements the waitgroup's count
// 3. returns the value returned from calling fn(args).
static void *wrap(void *args) {
  struct task *task = (struct task *)args;
  void *result = task->fn(task->args);
  wg_done(task->wg);
  free(task);
  return result;
}

// Spawn a fiber and add it to the waitgroup
fiber_t *wg_spawn(waitgroup_t *wg, void *(*fn)(void *), void *args,
                  void **result) {
  wg_add(wg, 1);
  // Wrap fn and its args into a function
  // that executes fn(args) and calls wg_done() 
  // before returning.
  struct task *task = (struct task *)malloc(sizeof(struct task));
  *task = (struct task){.fn = fn, .args = args, .wg = wg};
  fiber_t *f = fiber_spawn((void *)wrap, (void *)task, 0, result);
  return f;
}

// Blocks until the waitgroup's count reaches 0
void wg_wait(waitgroup_t *wg) {
  if (wg->count == 0) {
    return;
  }
  fiber_t *self = sched->curr;
  self->state = BLOCKED;
  enqueue(&wg->wait_q, self);
  switch_context(&sched->curr->context, &sched->self->context);
  // Here, we know the calling ctx is a fiber that was explicitly spawned.
  // We can't assume it is dead, so don't free the stack yet.
  return;
}
