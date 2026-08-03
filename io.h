#ifndef IO_H
#define IO_H

#include "fiber.h"
#include "queue.h"
#include "ringbuf.h"

#define MAX_FDS 1024
#define READERSQ 0;
#define WRITERSQ 1;

typedef struct {
  fiber_t *curreader;
  fiber_t *curwriter;
  node_t *waitq;
} ioreq_t;

typedef struct {
  int lastenqueued;
  fiber_t *curreader;
  fiber_t *curwriter;
  ringbuf_t *readersq;
  ringbuf_t *writersq;
  node_t *waitq;
} fd_waiters_t;

extern fd_waiters_t iorequests[MAX_FDS];

void ioq_remove(node_t **head, fiber_t *f);

int closefd(int fd);

#endif
