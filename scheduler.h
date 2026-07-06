#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "fiber.h"
#include "queue.h"
#include "ringbuf.h"

typedef struct {
  ringbuf_t *runq;
  fiber_t *running;
  fiber_t * self;
  int nready;
} scheduler_t;

extern scheduler_t *sched;

int sched_init(void);
void sched_run(void);
int sched_start(void (*main)(int, char**), int argc, char **argv);
void wakeall(node_t **head);

#endif
