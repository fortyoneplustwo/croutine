#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "fiber.h"
#include "queue.h"

typedef struct {
  void *run_q;
  fiber_t *curr;
  fiber_t *self;
  int netpollfd;
} scheduler_t;

extern scheduler_t *sched;

int sched_init(void);
int sched_run(void);
void sched_start(void);
void wakeall(node_t **head);

#endif
