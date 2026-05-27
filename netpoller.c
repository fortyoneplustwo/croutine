#include "netpoller.h"
#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

netpoller_t *self;

void renqueue(req_t *q, req_t *r) {
  if (!q) {
    q = r;
    return;
  }
  req_t *cur = (req_t *)q;
  while (cur->next) {
    cur = cur->next;
  }
  cur->next = r;
}

req_t *rdequeue(req_t *q) {
  if (!q) {
    return NULL;
  }
  req_t *first = (req_t *)q;
  q = (void *)first->next;
  return first;
}

int evcmp(struct epoll_event *src, struct epoll_event *dst) {
  return src->events == dst->events && src->data.ptr == dst->data.ptr &&
         src->data.fd == dst->data.fd && src->data.u32 == dst->data.u32;
}

req_t *rremove(req_t *q, struct epoll_event *ev) {
  if (!q) {
    return NULL;
  }
  if (evcmp(ev, &q->ev)) {
    req_t *first = q;
    q = q->next;
    return first;
  }
  req_t *cur = q;
  while (cur->next) {
    req_t *next = (req_t *)cur->next;
    if (evcmp(ev, &next->ev)) {
      cur->next = next->next;
      return cur;
    }
  }
  return NULL;
}

netpoller_t *np_init() {
  self = (netpoller_t *)calloc(1, sizeof(netpoller_t));
  if (!self) {
    fprintf(stderr, "could not allocate memory for netpoller\n");
    return NULL;
  }

  int epollfd = epoll_create(1);
  if (epollfd < 0) {
    perror("could not create epollfd");
    return NULL;
  }
  self->fd = epollfd;

  for (int i = 0; i < MAX_EVENTS; i++) {
    self->fdregistry[i] = -1;
  }

  self->fid = -1;

  return self;
}

// Imagine a fiber has called fread()
// Then fread() will attempt to register with netpoller
// assume registration is a success.
// then it will put itself on fd's io queue and yield().
// when netpoller is picked to run by the scheduler
// it will call epoll_wait() and return which fds are ready
// now we can simply check fd's io queue and put the next fiber onto the jobq
// when that fiber continues execution of fread() and succeeds,
// it will remove itself from fd's io queue before returning.
// because otherwise if it doesn't succeed, or is not finished, then we would
// like to remain on the queue so that we can be picked up again the next time
// the fd is ready.
void np_run() {
  struct epoll_event ev;
  while (1) {
    // Process any events that are ready (level-triggered)
    // 50ms timeout
    int nfds = epoll_wait(self->fd, self->events, MAX_EVENTS, 50);
    if (nfds == -1) {
      self->nready = errno;
      // error
    } else {
      self->nready = 0;
    }
    fiber_yield();
  }
}

int np_reg(int fd, struct epoll_event *ev) {
  // hasn't been registered at all. must be added
  if (self->fdregistry[fd] == -1) {
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
      return -1;
    }
    return epoll_ctl(self->fd, EPOLL_CTL_ADD, ev->data.fd, ev);
  }
  // has been registered already, but possibly need to amend events?
  if (self->fdregistry[fd] != ev->events) {
    return epoll_ctl(self->fd, EPOLL_CTL_MOD, ev->data.fd, ev);
  }
  return 0;
}
