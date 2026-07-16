#include <stdint.h>

typedef struct {
  // stack pointer
  uintptr_t rsp;
  // base pointer
  uintptr_t rbp;
  uintptr_t rbx;
  // general purpose
  uintptr_t r12;
  uintptr_t r13;
  uintptr_t r14;
  uintptr_t r15;
  // arguments
  // uintptr_t rdi;
  // uintptr_t rsi;
} context_t;

typedef struct {
  context_t context;
  void *stack;
} fiber_t;


