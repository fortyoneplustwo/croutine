#include "ringbuf.h"
#include "fiber.h"
#include <stdlib.h>

ringbuf_t *rbuf_init(uint32_t initial_capacity) {
  void **internal_buf = (void **)malloc(sizeof(void *) * initial_capacity);
  if (!internal_buf) {
    return NULL;
  }
  ringbuf_t *rb = (ringbuf_t *)calloc(1, sizeof(ringbuf_t));
  if (!rb) {
    free(internal_buf);
    return NULL;
  }
  rb->buf = internal_buf;
  rb->capacity = initial_capacity;
  return rb;
}

int rb_enqueue(ringbuf_t *rb, void *item) {
  if ((rb->tail + 1) % rb->capacity == rb->head) {
    void **newbuf = (void **)realloc(rb->buf, rb->capacity * 2);
    if (!newbuf) {
      return -1;
    }
    rb->capacity *= 2;
    rb->buf = newbuf;
  }
  rb->tail = (rb->tail + 1) % rb->capacity;
  rb->buf[rb->tail] = item;
  return 0;
}

void *rb_dequeue(ringbuf_t *rb) {
  if (rb->head == rb->tail) {
    return NULL;
  }
  rb->head = (rb->head + 1) % rb->capacity;
  return rb->buf[rb->head++];
}

int rb_prepend(ringbuf_t *rb, void *item) {
  if ((rb->tail + 1) % rb->capacity == rb->head) {
    return -1;
  }
  rb->head = (rb->head - 1);
  if (rb->head < 0) {
    rb->head += rb->capacity;
  }
  rb->buf[rb->head] = item;
  return 0;
}

void freerbuf(ringbuf_t *rb) {
  if (!rb) {
    return;
  }
  if (rb->buf) {
    free(rb->buf);
  }
  free(rb);
  rb = NULL;
}
