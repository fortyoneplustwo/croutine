#ifndef SYNC_H
#define SYNC_H

#include "fiber.h"
#include "queue.h"
#include "scheduler.h"

/*
 * Wait group
 * */
typedef struct {
  int count;
  node_t *wait_q;
} waitgroup_t;

waitgroup_t wg_make(void);
void wg_add(waitgroup_t *wg, int n);
void wg_done(waitgroup_t *wg);
void wg_wait(waitgroup_t *wg);
fiber_t *wg_spawn(waitgroup_t *wg, void *(*fn)(), void *args, void **result);

/*
 * Channel
 * */
typedef struct {
  node_t *recv_q;
  node_t *send_q;
  void *data;
  int closed;
} channel_t;

channel_t chan_make();
int chan_send(channel_t *ch, void *data);
int chan_recv(channel_t *ch, void **result);
void chan_close(channel_t *ch);

#endif
