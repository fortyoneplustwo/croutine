#include "netpoller.h"
#include "fiber.h"
#include "io.h"
#include "queue.h"
#include "ringbuf.h"
#include "runtime.h"
#include "scheduler.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>

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
    np->registered_events[i] = 0;
  }
  np->fid = -1;
  return np;
}

int unregisterev(int fd, uint32_t ev) {
  uint32_t evremoved = np->registered_events[fd] & ~ev;
  struct epoll_event newev = (struct epoll_event){
      .events = evremoved,
      .data = {.fd = fd},
  };
  if (np_reg(fd, &newev) == -1) {
    perror("failed to unregister event with no matching fiber");
    return -1;
  }
  np->registered_events[fd] = evremoved;
  return 0;
}

void np_run() {
  while (1) {
    // printf("polling for I/O\n");
    int shouldblock = sched->nready == 0 ? -1 : 0;
    np->nready = epoll_wait(np->fd, np->events, MAX_EVENTS, shouldblock);

    if (np->nready == -1) {
      exit(3);
    }

    // dispatch events
    for (int i = 0; i < np->nready; i++) {
      uint32_t readyev = (&np->events[i])->events;
      uint32_t readyfd = (&np->events[i])->data.fd;

      if ((readyev & NPIN) == NPIN) {
        fiber_t *reader = iorequests[readyfd].curreader;
        if (!reader) {
          unregisterev(readyfd, readyev);
        } else {
          enqueue(&sched->runq, reader);
          reader->state = READY;
          sched->nready++;
        }
      }

      if ((readyev & NPOUT) == NPOUT) {
        fiber_t *writer = iorequests[readyfd].curwriter;
        if (!writer) {
          unregisterev(readyfd, readyev);
        } else {
          enqueue(&sched->runq, writer);
          writer->state = READY;
          sched->nready++;
        }
      }
    }

    fiber_yield();
  }
}

// TODO: fix this, it's messy
int np_reg(int fd, struct epoll_event *ev) {
  int err;
  if (np->registered_events[fd] == 0) {
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
      return -1;
    }
    ev->events |= EPOLLET;
    err = epoll_ctl(np->fd, EPOLL_CTL_ADD, fd, ev);
    np->registered_events[fd] = ev->events & ~EPOLLET;
    return err;
  }
  if ((np->registered_events[fd] & ev->events) != ev->events) {
    ev->events |= np->registered_events[fd];
    ev->events |= EPOLLET;
    err = epoll_ctl(np->fd, EPOLL_CTL_MOD, fd, ev);
    np->registered_events[fd] = ev->events & ~EPOLLET;
    return err;
  }
  return 0;
}
