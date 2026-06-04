#ifndef FIBER_H
#define FIBER_H

#include "context.h"
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#define STACK_SIZE 2048

extern int count;

typedef enum {
  READY,
  RUNNING,
  YIELDED,
  DEAD,
  BLOCKED,
} fiber_state_t;

typedef struct fiber_t {
  int id;
  fiber_state_t state;
  uint32_t events;
  int ownedfd;
  void *waitlist;
  context_t caller;
  context_t context;
  void *stack;
  void (*entry)(void *);
  void *args;
  size_t len;
  void *msg;
} fiber_t;

fiber_t *fiber_create(void (*entry)(), void *args, int id);
void fiber_spawn(void (*entry)(), void *args);
void fiber_run(fiber_t *f);
void fiber_yield(void);
void fstack_free(fiber_t *f);
void fiber_await(fiber_t *f);

// IO stuff
ssize_t fiber_read(int fd, void *buf, size_t count);
ssize_t fiber_write(int fd, void *buf, size_t count);
int fiber_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int fiber_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

void switch_context_as_entry(void *arg);

#endif
