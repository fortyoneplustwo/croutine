#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include "scheduler.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

extern void switch_context(context_t *, context_t *);

int count = 1;

void switch_context_as_entry(void *arg) {
  context_t old;
  context_t *new = (context_t *)arg;
  sched->curr->state = DEAD;
  sched->curr = NULL;
  switch_context(&old, new);
}

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
  // Add padding for the Red Zone
  stack = (uint64_t *)stack - 128;
  // Push trampoline onto the stack
  stack = (uint64_t *)stack - 1;
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
  struct epoll_event ev =
      (struct epoll_event){.events = EPOLLIN, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->curr->events = ev.events;
  while (1) {
    ssize_t n = read(fd, buf, count);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (ioreqs[fd].curreader != sched->curr) {
        printf("not ready to read\n");
        enqueue(&ioreqs[fd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      ioreqs[fd].curreader = sched->curr;
      continue;
    }
    if (n <= 0) {
      ioq_remove(&ioreqs[fd].waitq, sched->curr);
      ioreqs[fd].curreader = NULL;
      sched->curr->events = 0;
    }
    return n;
  }
}

ssize_t fiber_write(int fd, void *buf, size_t count) {
  struct epoll_event ev =
      (struct epoll_event){.events = EPOLLOUT, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->curr->events = ev.events;
  while (1) {
    ssize_t n = write(fd, buf, count);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (ioreqs[fd].curwriter != sched->curr) {
        enqueue(&ioreqs[fd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      ioreqs[fd].curwriter = sched->curr;
      continue;
    }
    if (n <= 0) {
      ioq_remove(&ioreqs[fd].waitq, sched->curr);
      sched->curr->events = 0;
      ioreqs[fd].curwriter = NULL;
    }
    return n;
  }
}

int fiber_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  struct epoll_event ev =
      (struct epoll_event){.events = EPOLLIN, .data.fd = sockfd};
  if (np_reg(sockfd, &ev) == -1) {
    return -1;
  }
  sched->curr->events = ev.events;
  while (1) {
    int result = accept(sockfd, addr, addrlen);
    if (result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (ioreqs[sockfd].curreader != sched->curr) {
        enqueue(&ioreqs[sockfd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      ioreqs[sockfd].curreader = sched->curr;
      continue;
    }
    ioreqs[sockfd].curreader = NULL;
    sched->curr->events = 0;
    ioq_remove(&ioreqs[sockfd].waitq, sched->curr);
    return result;
  }
}

int fiber_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  struct epoll_event ev = (struct epoll_event){
      .events = EPOLLOUT,
      .data.fd = sockfd,
  };
  if (np_reg(sockfd, &ev) == -1) {
    return -1;
  }
  sched->curr->events = ev.events;
  while (1) {
    int result = connect(sockfd, addr, addrlen);
    if (result == -1 && (result == EAGAIN || result == EINPROGRESS)) {
      if (ioreqs[sockfd].curwriter != sched->curr) {
        enqueue(&ioreqs[sockfd].waitq, sched->curr);
      }
      switch_context(&sched->curr->context, &sched->self->context);
      ioreqs[sockfd].curwriter = sched->curr;
      continue;
    }
    int status;
    socklen_t status_size = sizeof(int);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &status, &status_size) == -1) {
      status = -1;
    }
    ioreqs[sockfd].curwriter = NULL;
    ioq_remove(&ioreqs[sockfd].waitq, sched->curr);
    sched->curr->events = 0;
    return status;
  }
  return 0;
}
