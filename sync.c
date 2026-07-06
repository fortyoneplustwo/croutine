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
// waitgroup_t *wg_make() { 
//   waitgroup_t *wg = (waitgroup_t *)calloc(1, sizeof(waitgroup_t)); 
//   if (!wg) {
//     return NULL;
//   }
//   ringbuf_t *waitq = rbuf_init(RBUFDEFAULTCAP);
//   if (!waitq) {
//     wg_free(wg);
//     return NULL;
//   }
//   wg->waitq;
//   return wg;
// }

// void wg_free(waitgroup_t *wg) {
//   free(wg);
//   wg = NULL;
// }

// Increments waitgroup's count by n.
// If the count goes negative return -1 or 0 otherwise.
// TODO: add to queue?
// int wg_add(waitgroup_t *wg, int n) {
//   wg->count += n;
//   if (wg->count < 0) {
//     return -1;
//   }
//   return 0;
// }

// Decrements the waitgroup's count.
// If count reaches 0, all waiting tasks are scheduled to run.
// void wg_done(waitgroup_t *wg) {
//   wg->count -= 1;
//   if (wg->count == 0) {
//     int i;
//     wakeall(&wg->wait_q);
//   }
// }

// struct task {
//   void *(*fn)(void *);
//   void *args;
//   waitgroup_t *wg;
// };

// Wraps the function arg `f` in a wrapper function that synchronously
// 1. executes `fn(args)`
// 2. decrements the waitgroup's count
// 3. returns the value returned from calling fn(args).
// static void wrap(void *args) {
//   struct task *task = (struct task *)args;
//   task->fn(task->args);
//   wg_done(task->wg);
//   free(task);
// }

// Spawn a fiber and add it to the waitgroup
// void wg_spawn(waitgroup_t *wg, void *(*fn)(void *), void *args) {
//   wg_add(wg, 1);
//   // Wrap fn and its args into a function
//   // that executes fn(args) and calls wg_done()
//   // before returning.
//   struct task *task = (struct task *)malloc(sizeof(struct task));
//   *task = (struct task){.fn = fn, .args = args, .wg = wg};
//   fiber_spawn(wrap, (void *)task);
// }

// Blocks until the waitgroup's count reaches 0
// void wg_wait(waitgroup_t *wg) {
//   if (wg->count == 0) {
//     return;
//   }
//   fiber_t *self = sched->running;
//   self->state = BLOCKED;
//   enqueue(&wg->wait_q, self);
//   switch_context(&sched->running->context, &sched->self->context);
//   // Here, we know the calling ctx is a fiber that was explicitly spawned.
//   // We can't assume it is dead, so don't free the stack yet.
//   return;
// }

/*
 * Channel
 * */

#define RBUFINITIALCAP 16

// Returns a new channel
channel_t *chan_make() {
  channel_t *ch = (channel_t *)calloc(1, sizeof(channel_t));
  if (!ch) {
    return NULL;
  }
  ringbuf_t *recvq = rbuf_init(RBUFINITIALCAP);
  if (!recvq) {
    return NULL;
  }
  ringbuf_t *sendq = rbuf_init(RBUFINITIALCAP);
  if (!sendq) {
    return NULL;
  }
  ch->recvq = recvq;
  ch->sendq = sendq;
  return ch;
}

void chan_free(channel_t *ch) {
  freerbuf(ch->recvq);
  freerbuf(ch->sendq);
  free(ch);
  ch = NULL;
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
    err = rb_enqueue(ch->sendq, self);
    if (err) {
      return -1;
    }
    ch->nsenders++;
    switch_context(&self->context, &sched->self->context);
    if (ch->isclosed) {
      return -1;
    }
    return 0;
  }
  ch->data = data;
  fiber_t *recver = rb_dequeue(ch->recvq);
  // fib should be defined
  // handle err anyway?
  ch->nrecvers--;
  recver->state = READY;
  rb_prepend(sched->runq, recver);
  sched->nready++;
  return 0;
}

// Receive data from a channel, but block until there is a sender.
// Returns 0 on success or -1 otherwise.
int chan_recv(channel_t *ch, void **result) {
  fiber_t *self = sched->running;
  if (!ch->data && ch->nsenders == 0) {
    self->state = BLOCKED;
    rb_enqueue(ch->recvq, self);
    ch->nrecvers++;
    switch_context(&self->context, &sched->self->context);
  }
  if (!ch->data) {
    if (ch->isclosed && ch->nsenders == 0) {
      return -1;
    }
    fiber_t *sender = rb_dequeue(ch->sendq);
    ch->nsenders--;
    *result = sender->msg;
    sender->state = READY;
    rb_prepend(sched->runq, sender);
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
    recver = rb_dequeue(ch->recvq);
    if (!recver) {
      // error
      return;
    }
    ch->nrecvers--;
    recver->state = READY;
    rb_enqueue(sched->runq, recver);
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
