#include "io.h"
#include "netpoller.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

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

int closefd(int fd) {
  int rc;
  // Pass a dummy, non-null event to epoll_ctl(EPOLL_CTL_DEL)
  // to maintain compatibility with old versions of linux.
  // See: `man close`
  struct epoll_event ev;
  if ((rc = epoll_ctl(np->fd, EPOLL_CTL_DEL, fd, &ev)) == -1) {
    return rc;
  }

  np->fdregistry[fd] = -1;

  if ((rc = close(fd)) == -1) {
    return rc;
  }

  if (ioreqs[fd].curreader == sched->curr) {
    ioreqs[fd].curreader = NULL;
  }
  if (ioreqs[fd].curwriter == sched->curr) {
    ioreqs[fd].curwriter = NULL;
  }

  return rc;
}
