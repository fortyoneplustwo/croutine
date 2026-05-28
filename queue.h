#ifndef QUEUE_H
#define QUEUE_H

typedef struct node_t {
  void *data;
  struct node_t *next;
} node_t;

void enqueue(node_t **p, void *x);
void *dequeue(node_t **p);
node_t *dequeue_node(node_t **p);
void *find(node_t *head, int (*pred)(void *x));
void push_front(node_t **p, void *x);
void append(node_t **p, node_t *q);
void prepend(node_t **p, node_t *n);
void prepend_rev(node_t **p, node_t *q);
// void *remove(node_t **p, void *x);

#endif
