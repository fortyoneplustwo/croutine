#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdint.h>

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

extern void switch_context(context_t *, context_t *);

#endif
