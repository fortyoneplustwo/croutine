#ifndef NETPOLLER_H
#define NETPOLLER_H

#include <sys/epoll.h>
#include "io.h"

#define MAX_EVENTS 10

typedef struct {
  int fid;
  int fd;
  int op;
  struct epoll_event ev;
  uint32_t events;
  void *next;
} req_t;

typedef struct {
  int fid;
  int fd;
  req_t *reqs_q;
  int maxfds;
  struct epoll_event events[MAX_EVENTS];
  int nready;
  int fdregistry[MAX_FDS];
} netpoller_t;

extern netpoller_t *np;

netpoller_t *np_init(void);
void np_run(void);
int np_reg(int fd, struct epoll_event *ev);

// functions for requests queue
req_t *rremove(req_t* reqs, struct epoll_event *ev);

#endif
