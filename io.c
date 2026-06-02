#include "io.h"
#include <stdlib.h>

ioreq_t ioreqs[MAX_FDS];

void ioq_remove(node_t **head, fiber_t *f) {
  node_t *cur = *head;
  node_t *prev = NULL;
  fiber_t *data = NULL;
  while (cur) {
    data = (fiber_t *)cur->data;
    if (data == f) {
      if (cur == *head) {
        *head = cur->next;
      } else {
        prev->next = cur->next;
      }
      free(cur);
      return;
    }
    prev = cur;
    cur = cur->next;
  }
}
