#include "runtime.h"
#include "fiber.h"
#include "scheduler.h"
#include "netpoller.h"
#include "queue.h"
#include "sync.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <asm-generic/socket.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


// void fiber_await(fiber_t *f) {
//   if (f->state == DEAD) {
//     fstack_free(f);
//     return;
//   }
//   fiber_t *self = sched->curr;
//   if (!self) {
//     self = fiber_create((void *)switch_context_as_entry,
//                         (void *)&sched->self->caller, 1, NULL, count++);
//     enqueue((node_t **)&f->waitlist, self);
//     self->state = BLOCKED;
//     switch_context(&sched->self->caller, &sched->self->context);
//     fstack_free(self);
//     return;
//   }
//   enqueue((node_t **)&f->waitlist, self);
//   self->state = BLOCKED;
//   switch_context(&self->context, &sched->self->context);
//   return;
// }


