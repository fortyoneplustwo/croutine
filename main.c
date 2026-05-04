#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

extern void switch_context(context_t*, context_t*);

void *thread_exit() { exit(1); }

// context struct
typedef struct {
  // stack pointer
  size_t rsp;
  // base pointer
  size_t rbp;
  size_t rbx;
  // general purpose
  size_t rb12;
  size_t rb13;
  size_t rb14;
  size_t rb15;
  // arguments
  // size_t rdi;
  // size_t rsi;

} context_t;

#define STACK_SIZE 16 * 256

extern void switch_context(context_t *old, context_t *new);

int main() {
  context_t old;
  context_t new = {0};

  // returns a 16-aligned
  size_t *stack = NULL;

  int result = posix_memalign((void **)&stack, 16, STACK_SIZE * sizeof(void *));

  if (result != 0) {
    printf("error allocating stack: %d\n", result);
  }

  stack += STACK_SIZE;
  // add padding
  stack -= 1;
  // add 128 padding for red zone
  stack -= 128;

  // push entry_fn to stack
  *(size_t *)stack = (size_t)thread_exit;

  // set new's stack pointer
  new.rsp = (size_t)stack;

  printf("about to switch\n");

  // guaranteed to be stored in %rsi and %rdi
  switch_context(&old, &new);

  printf("this shouldn't print out\n");

  return 0;
}
