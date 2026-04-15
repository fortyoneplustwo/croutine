#include <stdint.h>
#include <stdio.h>

#define STACK_SIZE 1024 * 64

typedef struct Context {
  uint64_t rbx;
  uint64_t rsp; // stack pointer
  uint64_t rbp; // base pointer
  uint64_t rdi; // arg1
  uint64_t rsi; // arg2
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip; // instruction pointer
} context_t;

extern void switch_context(context_t *to, context_t *from);

context_t from = {0};
context_t to = {0};

// function to run in new context
void greeting() {
  printf("Hello from new context\n");
  printf("About to switch back to main\n");
  switch_context(&from, &to);
  printf("Should not reach here\n");
}

int main() {
  // create stack for new context
  static uint8_t stack[STACK_SIZE];

  // the stack grows downwards, so point rsp to the end
  // MAKE SURE IT IS 16-byte aligned!
  to.rsp = ((uint64_t)(stack + STACK_SIZE)) & ~0xF;
	// NOTE: need to push a return address onto the stack?

  // set next instruction to run our greeting
  to.rip = (uint64_t)greeting;

  printf("About to switch to new context\n");

  switch_context(&to, &from);

  printf("Back to main\n");

  return 0;
}
