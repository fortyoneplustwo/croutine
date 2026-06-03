#ifndef SYNC_H
#define SYNC_H

#include "fiber.h"
#include "queue.h"
#include "scheduler.h"

typedef struct {
  int count;
  node_t *wait_q;
} waitgroup_t;

waitgroup_t wg_make(void);
void wg_add(waitgroup_t *wg, int n);
void wg_done(waitgroup_t *wg);
void wg_wait(waitgroup_t *wg);
fiber_t *wg_spawn(waitgroup_t *wg, void *(*fn)(), void *args, void **result);

#endif
