#include "ringbuf.h"
#include "fiber.h"
#include <stdlib.h>
#include <string.h>

ringbuf_t *rbuf_init(uint32_t size) {
  if (size << 31) { // size must be a power of 2
    return NULL;
  }
  void **internal_buf = (void **)malloc(sizeof(void *) * size);
  if (!internal_buf) {
    return NULL;
  }
  ringbuf_t *rb = (ringbuf_t *)calloc(1, sizeof(ringbuf_t));
  if (!rb) {
    free(internal_buf);
    return NULL;
  }
  rb->buf = internal_buf;
  rb->cap = size;
  return rb;
}

static inline ringbuf_t *reallocrbuf(ringbuf_t *rb) {
  void **newbuf = (void **)malloc(sizeof(void *) * rb->cap * 2);
  if (!newbuf) {
    return NULL;
  }
  if (rb->head == 0 && rb->tail == rb->cap - 1) {
    memcpy(newbuf, rb->buf, sizeof(void *) * (rb->cap - 1));
  } else {
    memcpy(newbuf, rb->buf + rb->head, sizeof(void *) * (rb->cap - rb->head));
    memcpy(newbuf + rb->cap - rb->head, rb->buf, sizeof(void *) * rb->tail);
  }
  free(rb->buf);
  rb->buf = newbuf;
  rb->head = 0;
  rb->tail = rb->cap - 1;
  rb->cap *= 2;
  return rb;
}

static inline int step(ringbuf_t *rb, int idx, int delta) {
  return (idx + delta) & (rb->cap - 1);
}

int rb_enqueue(ringbuf_t *rb, void *v) {
  if (step(rb, rb->tail, 1) == rb->head) {
    ringbuf_t *newrb = reallocrbuf(rb);
    if (!rb) {
      return -1;
    }
    rb = newrb;
  }
  rb->buf[rb->tail] = v;
  rb->tail = step(rb, rb->tail, 1);
  return 0;
}

void *rb_dequeue(ringbuf_t *rb) {
  if (rb->head == rb->tail) {
    return NULL;
  }
  void *first = rb->buf[rb->head];
  rb->buf[rb->head] = NULL;
  rb->head = step(rb, rb->head, 1);
  return first;
}

int rb_prepend(ringbuf_t *rb, void *v) {
  if (step(rb, rb->tail, 1) == rb->head) {
    ringbuf_t *newrb = reallocrbuf(rb);
    if (!rb) {
      return -1;
    }
    rb = newrb;
  }
  rb->head = step(rb, rb->head, -1);
  rb->buf[rb->head] = v;
  return 0;
}

void *rb_poplast(ringbuf_t *rb) {
  if (rb->head == rb->tail) {
    return NULL;
  }
  int lastidx = step(rb, rb->tail, -1);
  rb->tail = lastidx;
  return rb->buf[lastidx];
}

void freerbuf(ringbuf_t *rb) {
  if (!rb) {
    return;
  }
  if (rb->buf) {
    free(rb->buf);
  }
  free(rb);
}
