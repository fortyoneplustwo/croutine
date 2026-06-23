#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "fiber.h"
#include "queue.h"

typedef struct {
  void *run_q;
  fiber_t *curr;
  fiber_t *self;
  int netpollfd;
  int nfibers;
} scheduler_t;

extern scheduler_t *sched;

int sched_init(void);
void sched_run(void);
int sched_start(void (*main)(void));
void wakeall(node_t **head);

#endif
