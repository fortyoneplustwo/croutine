#include "sync.h"
#include "runtime.h"
#include "fiber.h"
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

void wg_wait(waitgroup_t *wg) {
  if (wg->count == 0) {
    return;
  }
  fiber_t *self = sched->curr;
  if (!self) {
    self = fiber_create((void *)switch_context_as_entry,
                        (void *)&sched->self->caller, 1, NULL, count++);
    self->state = BLOCKED;
    enqueue(&wg->wait_q, self);
    switch_context(&sched->self->caller, &sched->self->context);
    // Fiber-self is really just a placeholder that points us back to here.
    // If we arrive here, then the placeholder has served its purpose.
    // So it's safe to free its stack.
    // We do this here because it is not guaranteed that we
    // will enter the runtime again where fiber cleanup typically happens.
    fstack_free(self);
    return;
  }
  self->state = BLOCKED;
  enqueue(&wg->wait_q, self);
  switch_context(&sched->self->caller, &sched->self->context);
  // Here, we know the calling ctx is a fiber that was explicitly spawned.
  // We can't assume it is dead, so don't free the stack yet.
  return;
}
