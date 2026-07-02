#include <asm-generic/errno.h>
#define _GNU_SOURCE

#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include "scheduler.h"
#include <asm-generic/errno-base.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

extern void switch_context(context_t *, context_t *);

int count = 1;

// Destroy the fiber's stack
void fstack_free(fiber_t *f) {
  printf("Destroying fiber %d's stack\n", f->id);
  if (f->stack) {
    free(f->stack);
    f->stack = NULL;
  }
}

static void fiber_trampoline(fiber_t *f) {
  f->entry(f->args);
  printf("Job done. Switching back to scheduler...\n");
  f->state = DEAD;
  switch_context(&f->context, &sched->self->context);
}

fiber_t *fiber_create(void (*entry)(), void *args, int id) {
  fiber_t *self = calloc(1, sizeof(fiber_t));
  if (!self) {
    fprintf(stderr, "Couldn't allocate memory for new fiber context\n");
    return NULL;
  }

  // Create new stack, 16 bytes aligned
  void *stack = NULL;
  int status = posix_memalign(&stack, 16, STACK_SIZE * sizeof(uint64_t));
  if (status != 0) {
    fprintf(stderr, "Error allocating mem for stack: %d\n", status);
    return NULL;
  }
  self->stack = stack;
  // Stack grows downward, so must point to the end of block
  stack = (uint64_t *)stack + STACK_SIZE;
  assert((uint64_t)stack % 16 == 0);
  // Add padding for the Red Zone
  stack = (uint64_t *)stack - 128;
  assert((uint64_t)stack % 16 == 0);
  // Push trampoline onto the stack
  // but add 8 bytes of padding so that
  // the stack pointer is 16 byte aligned
  // just before entry is called
  stack = (uint64_t *)stack - 2;
  assert((uint64_t)stack % 8 == 0);
  *(uint64_t *)stack = (uint64_t)fiber_trampoline;
  // Set argument of trampoline (rdi)
  self->context.rdi = (uint64_t)self;
  // Set stack pointer (rsp)
  self->context.rsp = (uint64_t)stack;
  // Set entry function
  self->entry = entry;
  // Set args of entry function
  self->args = args;
  // Set id
  self->id = id++;

  return self;
}

void fiber_spawn(void (*entry)(), void *args) {
  fiber_t *self = fiber_create(entry, args, count++);
  push_front((node_t **)&sched->run_q, self);
  sched->nfibers++;
  printf("Spawned fiber %d\n", self->id);
}

void fiber_run(fiber_t *f) {
  printf("Fiber %d: ", f->id);
  sched->curr = f;
  f->state = RUNNING;
  switch_context(&sched->self->context, &f->context);
}

void fiber_yield() {
  sched->curr->state = YIELDED;
  switch_context(&sched->curr->context, &sched->self->context);
}

ssize_t fiber_read(int fd, void *buf, size_t count) {
  struct epoll_event ev = (struct epoll_event){.events = NPIN, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->curr->expectev = (fiber_event_t){.fd = fd, .event = NPIN};
  while (1) {
    ssize_t n = read(fd, buf, count);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (iorequests[fd].curreader != sched->curr) {
        sched->curr->state = BLOCKED;
        enqueue(&iorequests[fd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      iorequests[fd].curreader = sched->curr;
      continue;
    }
    ioq_remove(&iorequests[fd].waitq, sched->curr);
    iorequests[fd].curreader = NULL;
    sched->curr->expectev = (fiber_event_t){0};
    return n;
  }
}

ssize_t fiber_write(int fd, void *buf, size_t count) {
  ssize_t n;
  struct epoll_event ev = (struct epoll_event){.events = NPOUT, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->curr->expectev = (fiber_event_t){.fd = fd, .event = NPOUT};
  while (1) {
    n = write(fd, buf, count);
    if (n != -1)
      break;
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      break;
    if (iorequests[fd].curwriter != sched->curr) {
      sched->curr->state = BLOCKED;
      enqueue(&iorequests[fd].waitq, sched->curr);
    }
    switch_context(&sched->curr->context, &sched->self->context);
    iorequests[fd].curwriter = sched->curr;
  }
  ioq_remove(&iorequests[fd].waitq, sched->curr);
  sched->curr->expectev = (fiber_event_t){0};
  iorequests[fd].curwriter = NULL;
  return n;
}

int fiber_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  int result;
  struct epoll_event ev = (struct epoll_event){
      .events = NPIN,
      .data.fd = sockfd,
  };
  if (np_reg(sockfd, &ev) == -1) {
    return -1;
  }
  sched->curr->expectev = (fiber_event_t){.fd = sockfd, .event = NPIN};
  while (1) {
    result = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
    if (result != -1)
      break;
    if (errno != EAGAIN || errno != EWOULDBLOCK)
      break;
    if (iorequests[sockfd].curreader != sched->curr) {
      sched->curr->state = BLOCKED;
      enqueue(&iorequests[sockfd].waitq, sched->curr);
    }
    switch_context(&sched->curr->context, &sched->self->context);
    iorequests[sockfd].curreader = sched->curr;
  }
  iorequests[sockfd].curreader = NULL;
  sched->curr->expectev = (fiber_event_t){0};
  ioq_remove(&iorequests[sockfd].waitq, sched->curr);
  return result;
}

int fiber_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  int result, connecterr;
  socklen_t result_size = sizeof(result);
  struct epoll_event ev = (struct epoll_event){
      .events = NPOUT,
      .data.fd = sockfd,
  };
  if (np_reg(sockfd, &ev) == -1) {
    return -1;
  }
  sched->curr->expectev = (fiber_event_t){.fd = sockfd, .event = NPOUT};
  while (1) {
    result = connect(sockfd, addr, addrlen);
    if (result != -1)
      break;
    if (errno != EAGAIN || errno != EINPROGRESS)
      break;
    connecterr = errno;
    if (iorequests[sockfd].curwriter != sched->curr) {
      sched->curr->state = BLOCKED;
      enqueue(&iorequests[sockfd].waitq, sched->curr);
    }
    switch_context(&sched->curr->context, &sched->self->context);
    iorequests[sockfd].curwriter = sched->curr;
    if (connecterr != EAGAIN)
      break;
  }
  if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &result, &result_size) != 0) {
    result = -1;
  }
  iorequests[sockfd].curwriter = NULL;
  ioq_remove(&iorequests[sockfd].waitq, sched->curr);
  sched->curr->expectev = (fiber_event_t){0};
  return result;
}
