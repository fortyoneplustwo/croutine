#include "sync.h"
#include "context.h"
#include "fiber.h"
#include "queue.h"
#include "ringbuf.h"
#include "runtime.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Wait group
 * */

// Returns a new waitgroup as an rvalue.
// A waitgroup is a counting semaphore.
waitgroup_t *wg_make() { 
  waitgroup_t *wg = (waitgroup_t *)calloc(1, sizeof(waitgroup_t)); 
  if (!wg) {
    return NULL;
  }
  return wg;
}

void freewg(waitgroup_t *wg) {
  if (wg == NULL) {
    return;
  }
  free(wg);
}

// Increments waitgroup's count by n.
// If the count goes negative return -1 or 0 otherwise.
int wg_add(waitgroup_t *wg, int n) {
  wg->count += n;
  if (wg->count < 0) {
    return -1;
  }
  return 0;
}

// Decrements the waitgroup's count.
// If count reaches 0, all waiting tasks are scheduled to run.
void wg_done(waitgroup_t *wg) {
  wg->count -= 1;
  if (wg->count == 0) {
    wakeall(&wg->waitq);
    wg->nwaiters = 0;
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
static void wrap(void *args) {
  struct task *task = (struct task *)args;
  task->fn(task->args);
  wg_done(task->wg);
  free(task);
}

// Spawn a fiber and add it to the waitgroup
void wg_spawn(waitgroup_t *wg, void *(*fn)(void *), void *args) {
  wg_add(wg, 1);
  // Wrap fn and its args into a function
  // that executes fn(args) and calls wg_done()
  // before returning.
  struct task *task = (struct task *)malloc(sizeof(struct task));
  *task = (struct task){.fn = fn, .args = args, .wg = wg};
  fiber_spawn(wrap, (void *)task);
}

// Blocks until the waitgroup's count reaches 0
void wg_wait(waitgroup_t *wg) {
  if (wg->count == 0) {
    return;
  }
  fiber_t *self = sched->running;
  self->state = BLOCKED;
  enqueue(&wg->waitq, self);
  wg->nwaiters++;
  switch_context(&sched->running->context, &sched->self->context);
  // Here, we know the calling ctx is a fiber that was explicitly spawned.
  // We can't assume it is dead, so don't free the stack yet.
  return;
}

/*
 * Channel
 * */

#define RBUFINITIALCAP 1 << 4

// Returns a new channel
channel_t *chan_make() {
  channel_t *ch = (channel_t *)calloc(1, sizeof(channel_t));
  if (!ch) {
    return NULL;
  }
  return ch;
}

void chan_free(channel_t *ch) {
  free(ch);
}

// Send data on a channel, but block until there is a receiver.
// Returns 0 on success or -1 otherwise.
int chan_send(channel_t *ch, void *data) {
  int err;
  if (ch->isclosed) {
    return -1;
  }
  fiber_t *self = sched->running;
  if (ch->nrecvers == 0 || ch->data) {
    self->msg = data;
    self->state = BLOCKED;
    enqueue(&ch->sendq, self);
    // if (err) {
    //   return -1;
    // }
    ch->nsenders++;
    switch_context(&self->context, &sched->self->context);
    if (ch->isclosed) {
      return -1;
    }
    return 0;
  }
  ch->data = data;
  node_t *node = dequeue_node(&ch->recvq);
  fiber_t *recver = (fiber_t *)node->data;
  // fib should be defined
  // handle err anyway?
  ch->nrecvers--;
  recver->state = READY;
  prepend(&sched->runq, node);
  sched->nready++;
  return 0;
}

// Receive data from a channel, but block until there is a sender.
// Returns 0 on success or -1 otherwise.
int chan_recv(channel_t *ch, void **result) {
  fiber_t *self = sched->running;
  if (!ch->data && ch->nsenders == 0) {
    self->state = BLOCKED;
    enqueue(&ch->recvq, self);
    ch->nrecvers++;
    switch_context(&self->context, &sched->self->context);
  }
  if (!ch->data) {
    if (ch->isclosed && ch->nsenders == 0) {
      return -1;
    }
    node_t *node = dequeue_node(&ch->sendq);
    fiber_t *sender = (fiber_t *)node->data;
    ch->nsenders--;
    *result = sender->msg;
    sender->state = READY;
    prepend(&sched->runq, node);
    sched->nready++;
    return 0;
  }
  *result = ch->data;
  ch->data = NULL;
  return 0;
}

// wakes all the fibers blocked on receive
static void chan_drain(channel_t *ch) {
  int i;
  fiber_t *recver = NULL;
  for (i = 0; i < ch->nrecvers; i++) {
    node_t *node = dequeue(&ch->recvq);
    recver = (fiber_t *)node->data;
    if (!recver) {
      // error
      return;
    }
    ch->nrecvers--;
    recver->state = READY;
    enqueue(&sched->runq, recver);
    sched->nready++;
  }
}

// Signal to close channel so any further sends will fail.
// Sending on a closed channel will fail.
// need to drain channel on close
// bc otherwise if there are fibers blocked on recieve, then they will never
// be unblocked bc there will be so sends.
void chan_close(channel_t *ch) {
  chan_drain(ch);
  ch->isclosed = 1;
}

void freechan(channel_t *ch) {
  if (ch == NULL) {
    return;
  }
  free(ch);
}
