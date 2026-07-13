#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>

#define RBUFDEFAULTCAP 1 << 6

typedef struct {
  int cap;
  int head;
  int tail;
  void **buf;
} ringbuf_t;

ringbuf_t *rbuf_init(uint32_t initial_cap);
int rb_enqueue(ringbuf_t *rb, void *item);
void *rb_dequeue(ringbuf_t *rb);
int rb_prepend(ringbuf_t *rb, void *item);
void *rb_poplast(ringbuf_t *rb);
void freerbuf(ringbuf_t *rb);

#endif
