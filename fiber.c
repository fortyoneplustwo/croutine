#include <asm-generic/errno.h>
#define _GNU_SOURCE

#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "queue.h"
#include "scheduler.h"
#include <assert.h>
#include <errno.h>
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

static int iscurfib(node_t *node) {
  fiber_t *data = (fiber_t *)node->data;
  return data == sched->running;
}

// Destroy the fiber's stack
void fstack_free(fiber_t *f) {
  printf("Destroying fiber %d's stack\n", f->id);
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
  // int status = posix_memalign(&stack, 16, STACK_SIZE);
  stack  = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
  fiber_t *self = fiber_create(entry, args, count++);
  push_front(&sched->runq, self);
  sched->nready++;
  printf("Spawned fiber %d\n", self->id);
}

void fiber_run(fiber_t *f) {
  printf("Fiber %d: ", f->id);
  sched->running = f;
  f->state = RUNNING;
  switch_context(&sched->self->context, &f->context);
}

void fiber_yield() {
  sched->running->state = YIELDED;
  switch_context(&sched->running->context, &sched->self->context);
}

ssize_t fiber_read(int fd, void *buf, size_t count) {
  ssize_t n;
  struct epoll_event ev = (struct epoll_event){.events = NPIN, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->running->expectev = (fiber_event_t){.fd = fd, .event = NPIN};
  while (1) {
    n = read(fd, buf, count);
    if (n != -1)
      break;
    if (errno != EAGAIN || errno != EWOULDBLOCK)
      break;
    if (iorequests[fd].curreader != sched->running) {
      sched->running->state = BLOCKED;
      enqueue(&iorequests[fd].waitq, sched->running);
    }
    switch_context(&sched->running->context, &sched->self->context);
    iorequests[fd].curreader = sched->running;
  }
  rmnode(&iorequests[fd].waitq, iscurfib);
  iorequests[fd].curreader = NULL;
  sched->running->expectev = (fiber_event_t){0};
  return n;
}

ssize_t fiber_write(int fd, void *buf, size_t count) {
  ssize_t n;
  struct epoll_event ev = (struct epoll_event){.events = NPOUT, .data.fd = fd};
  if (np_reg(fd, &ev) == -1) {
    return -1;
  }
  sched->running->expectev = (fiber_event_t){.fd = fd, .event = NPOUT};
  while (1) {
    n = write(fd, buf, count);
    if (n != -1)
      break;
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      break;
    if (iorequests[fd].curwriter != sched->running) {
      sched->running->state = BLOCKED;
      enqueue(&iorequests[fd].waitq, sched->running);
    }
    switch_context(&sched->running->context, &sched->self->context);
    iorequests[fd].curwriter = sched->running;
  }
  rmnode(&iorequests[fd].waitq, iscurfib);
  sched->running->expectev = (fiber_event_t){0};
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
  sched->running->expectev = (fiber_event_t){.fd = sockfd, .event = NPIN};
  while (1) {
    result = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
    if (result != -1)
      break;
    if (errno != EAGAIN || errno != EWOULDBLOCK)
      break;
    if (iorequests[sockfd].curreader != sched->running) {
      sched->running->state = BLOCKED;
      enqueue(&iorequests[sockfd].waitq, sched->running);
    }
    switch_context(&sched->running->context, &sched->self->context);
    iorequests[sockfd].curreader = sched->running;
  }
  iorequests[sockfd].curreader = NULL;
  sched->running->expectev = (fiber_event_t){0};
  rmnode(&iorequests[sockfd].waitq, iscurfib);
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
  sched->running->expectev = (fiber_event_t){.fd = sockfd, .event = NPOUT};
  while (1) {
    result = connect(sockfd, addr, addrlen);
    if (result != -1)
      break;
    if (errno != EAGAIN || errno != EINPROGRESS)
      break;
    connecterr = errno;
    if (iorequests[sockfd].curwriter != sched->running) {
      sched->running->state = BLOCKED;
      enqueue(&iorequests[sockfd].waitq, sched->running);
    }
    switch_context(&sched->running->context, &sched->self->context);
    iorequests[sockfd].curwriter = sched->running;
    if (connecterr != EAGAIN)
      break;
  }
  if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &result, &result_size) != 0) {
    result = -1;
  }
  iorequests[sockfd].curwriter = NULL;
  rmnode(&iorequests[sockfd].waitq, iscurfib);
  sched->running->expectev = (fiber_event_t){0};
  return result;
}
