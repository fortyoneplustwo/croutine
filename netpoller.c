#include "netpoller.h"
#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

netpoller_t *np;

netpoller_t *np_init() {
  np = (netpoller_t *)calloc(1, sizeof(netpoller_t));
  if (!np) {
    fprintf(stderr, "could not allocate memory for netpoller\n");
    return NULL;
  }
  int epollfd = epoll_create(1);
  if (epollfd < 0) {
    perror("could not create epollfd");
    return NULL;
  }
  np->fd = epollfd;
  for (int i = 0; i < MAX_FDS; i++) {
    np->registered_events[i] = -1;
  }
  np->fid = -1;
  return np;
}

void np_run() {
  while (1) {
    printf("polling for I/O\n");
    int nready = epoll_wait(np->fd, np->events, MAX_EVENTS, 50);
    if (nready == -1) {
      np->nready = -1;
    } else {
      np->nready = nready;
    }
    fiber_yield();
  }
}

int np_reg(int fd, struct epoll_event *ev) {
  int err;
  if (np->registered_events[fd] == -1) {
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
      return -1;
    }
    err = epoll_ctl(np->fd, EPOLL_CTL_ADD, fd, ev);
    np->registered_events[fd] = ev->events;
    return err;
  }
  if ((np->registered_events[fd] & ev->events) != ev->events) {
    ev->events |= np->registered_events[fd];
    err = epoll_ctl(np->fd, EPOLL_CTL_MOD, fd, ev);
    np->registered_events[fd] = ev->events;
    return err;
  }
  return 0;
}
