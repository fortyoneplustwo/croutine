#ifndef IO_H
#define IO_H

#include "fiber.h"
#include "queue.h"

#define MAX_FDS 1024

typedef struct {
  fiber_t *curreader;
  fiber_t *curwriter;
  node_t *waitq;
} ioreq_t;

extern ioreq_t iorequests[MAX_FDS];

void ioq_remove(node_t **head, fiber_t *f);

int closefd(int fd);

#endif
