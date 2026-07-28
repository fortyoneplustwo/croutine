#define _GNU_SOURCE

#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include "ringbuf.h"
#include "scheduler.h"
#include <asm-generic/errno.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

extern void switch_context(context_t *, context_t *);

int count = 1;

// Destroy the fiber's stack
void fstack_free(fiber_t *f) {
  // printf("Destroying fiber %d's stack\n", f->id);
  if (f->stack) {
    if (munmap(f->stack, 1) == -1) {
      perror("failed to unmap fiber stack guard");
    };
    // free(f->stack);
    f->stack = NULL;
  }
}

static void fiber_trampoline(fiber_t *f) {
  f->entry(f->args);
  // printf("Job done. Switching back to scheduler...\n");
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
  // int status = posix_memalign(&stack, 16, STACK_SIZE);
  stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if ((int64_t)stack == -1) {
    fprintf(stderr, "Error allocating mem for stack: %d\n", errno);
    return NULL;
  }
  // Add guard
  int err = mprotect(stack, sysconf(_SC_PAGESIZE), PROT_NONE);
  if (err) {
    perror("failed to set stack guard");
    return NULL;
  }
  self->stack = stack;
  // Stack grows downward, so must point to the end of block
  stack = (char *)stack + STACK_SIZE;
  stack = (void *)((uint64_t)stack & ~0xF);
  assert((uint64_t)stack % 16 == 0);
  // Add padding for the Red Zone
  stack = (char *)stack - 128;
  assert((uint64_t)stack % 16 == 0);
  // Push trampoline onto the stack
  // but add 8 bytes of padding so that
  // the stack pointer is 16 byte aligned
  // just before entry is called
  stack = (uint64_t *)stack - 2;
  assert((uint64_t)stack % 16 == 0);
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
  int err;
  fiber_t *self = fiber_create(entry, args, count++);
  err = rb_prepend(sched->runq, self);
  if (err) {
    // TODO: handle error
  }
  // push_front(&sched->runq, self);
  sched->nready++;
  // printf("Spawned fiber %d\n", self->id);
}

void fiber_run(fiber_t *f) {
  // printf("Fiber %d: ", f->id);
  sched->running = f;
  f->state = RUNNING;
  switch_context(&sched->self->context, &f->context);
}

void fiber_yield() {
  sched->running->state = YIELDED;
  switch_context(&sched->running->context, &sched->self->context);
}

ssize_t fiber_read(int fd, void *buf, size_t len) {
  struct epoll_event ev = (struct epoll_event){.events = NPIN, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }

  if (iorequests[fd].curreader == NULL) {
    iorequests[fd].curreader = sched->running;
  }
  if (iorequests[fd].curreader != sched->running) {
    rb_enqueue(iorequests[fd].readersq, sched->running);
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
    /* woken up: previous owner already made us curreader before waking us */
  }

  ssize_t nread;
  while (1) {
    nread = read(fd, buf, len);
    if (nread >= 0) {
      break;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      break;
    }
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
  }

  fiber_t *next = rb_dequeue(iorequests[fd].readersq);
  iorequests[fd].curreader = next;
  if (next) {
    next->state = READY;
    rb_enqueue(sched->runq, next);
    sched->nready++;
  }
  return nread;
}

ssize_t fiber_write(int fd, void *buf, size_t len) {
  struct epoll_event ev = (struct epoll_event){.events = NPOUT, .data.fd = fd};
  if (np_reg(fd, &ev) == -1)
    return -1;

  if (iorequests[fd].curwriter == NULL) {
    iorequests[fd].curwriter = sched->running;
  }
  if (iorequests[fd].curwriter != sched->running) {
    rb_enqueue(iorequests[fd].writersq, sched->running);
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
    // on wake, it means we own the lock already
  }

  size_t nwrote = 0;
  while (nwrote < len) {
    ssize_t n = write(fd, (char *)buf + nwrote, len - nwrote);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      sched->running->state = BLOCKED;
      switch_context(&sched->running->context, &sched->self->context);
      continue;
    }
    if (n <= 0) {
      break;
    }
    nwrote += n;
  }

  fiber_t *next = rb_dequeue(iorequests[fd].writersq);
  iorequests[fd].curwriter = next;
  if (next) {
    next->state = READY;
    rb_enqueue(sched->runq, next);
    sched->nready++;
  }
  if (nwrote == len || nwrote > 0) {
    return nwrote;
  }
  return -1;
}

int fiber_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  struct epoll_event ev = (struct epoll_event){
      .events = NPIN,
      .data.fd = sockfd,
  };
  if (np_reg(sockfd, &ev) == -1) {
    return -1;
  }

  // try acquire lock if not held
  if (iorequests[sockfd].curreader == NULL) {
    iorequests[sockfd].curreader = sched->running;
  }
  if (iorequests[sockfd].curreader != sched->running) {
    rb_enqueue(iorequests[sockfd].readersq, sched->running);
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
    // NOTE: on wake, we have acquired the lock
  }

  int accfd;
  while (1) {
    accfd = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
    if (accfd != -1) {
      break;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      break;
    }
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
  }

  // NOTE: always dequeue the next waiter to preserve the invariant
  fiber_t *next = rb_dequeue(iorequests[sockfd].readersq);
  iorequests[sockfd].curreader = next;

  // WARN: should we enqueue next if we know the fd had an error?
  // yes we should because
  // 1. we have no idea how the next waiter might want to handle an error
  //    i.e. it may want to do some processing rather than keep waiting
  //    until the fd is free again
  // 2. the fd might actually become available between now
  //    and when the next waiter gets picked to run
  if (next) {
    next->state = READY;
    rb_enqueue(sched->runq, next);
    sched->nready++;
  }

  return accfd;
}

int fiber_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  struct epoll_event ev = (struct epoll_event){
      .events = NPOUT,
      .data.fd = sockfd,
  };
  if (np_reg(sockfd, &ev) == -1) {
    return -1;
  }

  if (iorequests[sockfd].curwriter == NULL) {
    iorequests[sockfd].curwriter = sched->running;
  }
  if (iorequests[sockfd].curwriter != sched->running) {
    rb_enqueue(iorequests[sockfd].writersq, sched->running);
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
    // on wake, it means we own the lock already
  }

  int err, connecterrno;
  while (1) {
    err = connect(sockfd, addr, addrlen);
    if (err != -1) {
      break;
    }
    if (errno != EAGAIN && errno != EINPROGRESS) {
      break;
    }
    connecterrno = errno;
    sched->running->state = BLOCKED;
    switch_context(&sched->running->context, &sched->self->context);
    if (connecterrno != EAGAIN) {
      break;
    }
  }

  socklen_t err_size = sizeof(err);
  if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &err_size) != 0) {
    err = -1;
  }

  fiber_t *next = NULL;
  if ((next = rb_dequeue(iorequests[sockfd].writersq))) {
    next->state = READY;
    rb_enqueue(sched->runq, next);
    sched->nready++;
  }
  return err;
}
