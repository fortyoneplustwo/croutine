#ifndef SYNC_H
#define SYNC_H

#include "fiber.h"
#include "queue.h"
#include "scheduler.h"
#include <stdint.h>

/*
 * Wait group
 * */
typedef struct {
  int count;
  node_t *wait_q;
} waitgroup_t;

waitgroup_t *wg_make(void);
int wg_add(waitgroup_t *wg, int n);
void wg_done(waitgroup_t *wg);
void wg_wait(waitgroup_t *wg);
void wg_spawn(waitgroup_t *wg, void *(*fn)(), void *args);
void wg_free(waitgroup_t *wg);

/*
 * Channel
 * */
typedef struct {
  node_t *recv_q;
  node_t *send_q;
  void *data;
  int closed;
  uint32_t len_recvq;
  uint32_t len_sendq;
} channel_t;

channel_t *chan_make(void);
int chan_send(channel_t *ch, void *data);
int chan_recv(channel_t *ch, void **result);
void chan_close(channel_t *ch);
void chan_free(channel_t *ch);

#endif
