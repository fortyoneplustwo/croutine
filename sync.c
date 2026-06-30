#include "sync.h"
#include "context.h"
#include "fiber.h"
#include "queue.h"
#include "runtime.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Wait group
 * */

// Returns a new waitgroup as an rvalue.
// A waitgroup is a counting semaphore.
waitgroup_t *wg_make() { return (waitgroup_t *)calloc(1, sizeof(waitgroup_t)); }

void wg_free(waitgroup_t *wg) {
  free(wg);
  wg = NULL;
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
  enqueue(&wg->wait_q, self);
  switch_context(&sched->running->context, &sched->self->context);
  // Here, we know the calling ctx is a fiber that was explicitly spawned.
  // We can't assume it is dead, so don't free the stack yet.
  return;
}

/*
 * Channel
 * */

// Returns a new channel
channel_t *chan_make() { return (channel_t *)calloc(1, sizeof(channel_t)); }

void chan_free(channel_t *ch) {
  free(ch);
  ch = NULL;
}

// Send data on a channel, but block until there is a receiver.
// Returns 0 on success or -1 otherwise.
int chan_send(channel_t *ch, void *data) {
  if (ch->closed) {
    return -1;
  }
  fiber_t *self = sched->running;
  if (!ch->recv_q || ch->data) {
    self->msg = data;
    enqueue(&ch->send_q, self);
    ch->len_sendq++;
    switch_context(&self->context, &sched->self->context);
    if (ch->closed) {
      return -1;
    }
    return 0;
  }
  ch->data = data;
  node_t *node = dequeue_node(&ch->recv_q);
  ch->len_recvq--;
  fiber_t *next = node->data;
  next->state = READY;
  prepend((node_t **)&sched->run_q, node);
  sched->nrunning++;
  return 0;
}

// Receive data from a channel, but block until there is a sender.
// Returns 0 on success or -1 otherwise.
int chan_recv(channel_t *ch, void **result) {
  fiber_t *self = sched->running;
  if (!ch->data && !ch->send_q) {
    enqueue(&ch->recv_q, self);
    ch->len_recvq++;
    switch_context(&self->context, &sched->self->context);
  }
  if (!ch->data) {
    if (ch->closed && ch->len_sendq == 0) {
      return -1;
    }
    node_t *node = dequeue_node(&ch->send_q);
    ch->len_sendq--;
    fiber_t *next = (fiber_t *)node->data;
    *result = next->msg;
    next->state = READY;
    prepend((node_t **)&sched->run_q, node);
    sched->running++;
    return 0;
  }
  *result = ch->data;
  ch->data = NULL;
  return 0;
}

// wakes all the fibers blocked on receive
static void chan_drain(channel_t *ch) {
  node_t *last = ch->recv_q;
  if (!last) {
    return;
  }
  while (last->next) {
    fiber_t *f = last->data;
    f->state = READY;
    last = last->next;
  }
  fiber_t *f = last->data;
  f->state = READY;
  last->next = sched->runq;
  sched->runq = ch->recv_q;
  sched->nrunning += ch->len_recvq;
  ch->len_recvq = 0;
}

// Signal to close channel so any further sends will fail.
// Sending on a closed channel will fail.
// need to drain channel on close
// bc otherwise if there are fibers blocked on recieve, then they will never
// be unblocked bc there will be so sends.
void chan_close(channel_t *ch) {
  chan_drain(ch);
  ch->closed = 1;
}
