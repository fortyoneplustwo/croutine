#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>
#include <sys/types.h>

typedef struct {
  // stack pointer
  uint64_t rsp;
  // base pointer
  uint64_t rbp;
  uint64_t rbx;
  // general purpose
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  // arguments
  uint64_t rdi;
} context_t;

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
  void *(*entry)(void *);
  void *args;
  size_t len;
  void **result; // pointer to a generic?
} fiber_t;

// scheduler functions
int sched_init(void);
int sched_run(void);
void sched_start(void);

// fiber functions
fiber_t *fiber_spawn(void *(*entry)(), void *args, size_t len, void **result);
void fiber_run(fiber_t *f);
void fiber_yield(void);
void fstack_free(fiber_t *f);
void fiber_await(fiber_t *f);
ssize_t fiber_read(int fd, void *buf, size_t count);
ssize_t fiber_write(int fd, void *buf, size_t count);


#endif
