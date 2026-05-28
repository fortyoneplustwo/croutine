#include "queue.h"
#include <stdlib.h>

void enqueue(node_t **p, void *x) {
  node_t *node = (node_t *)calloc(1, sizeof(node_t));
  node->data = x;
  if (!(*p)) {
    *p = node;
    return;
  }
  node_t *cur = *p;
  while (cur->next) {
    cur = cur->next;
  }
  cur->next = node;
}

void *dequeue(node_t **p) {
  if (!(*p)) {
    return NULL;
  }
  node_t *head = *p;
  void *data = head->data;
  *p = head->next;
  free(head);
  return data;
}

node_t *dequeue_node(node_t **p) {
  if (!(*p)) {
    return NULL;
  }
  node_t *head = *p;
  *p = head->next;
  return head;
}

void *find(node_t *head, int (*pred)(void *x)) {
  if (!head) {
    return NULL;
  }
  node_t *cur = head;
  while (!cur) {
    if (pred(cur->data) == 1) {
      return cur->data;
    }
  }
  return NULL;
}

void push_front(node_t **p, void *x) {
  node_t *node = (node_t *)calloc(1, sizeof(node_t));
  node->data = x;
  if (!(*p)) {
    *p = node;
    return;
  }
  node->next = *p;
  *p = node;
}

void append(node_t **p, node_t *n) {
  if (!(*p)) {
    *p = n;
    return;
  }
  node_t *cur = *p;
  while (cur->next) {
    cur = cur->next;
  }
  cur->next = n;
}

void prepend(node_t **head, node_t *n) {
  n->next = *head;
  *head = n;
}

void prepend_rev(node_t **p, node_t *q) {
  if (!q) {
    return;
  }
  if (!(*p)) {
    *p = NULL;
  }
  node_t *head = *p;
  node_t *cur = q;
  while (cur) {
    node_t *next = cur->next;
    cur->next = head;
    head = cur;
    cur = next;
  }
  *p = head;
}

