#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define STACK_SIZE 256

// context struct
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

typedef struct {
  int id;
  context_t caller;
  context_t context;
  void *stack;
  void *(*entry)(void *);
  void *args;
  size_t len;
  void **result;
} fiber_t;

extern void switch_context(context_t *, context_t *);

context_t old;
context_t new = {0};

int count = 0;

void fiber_destroy(fiber_t *f) {
  printf("Destroying fiber %d\n", f->id);
  if (f->stack) {
    free(f->stack);
  }
}

void fiber_trampoline(fiber_t *f) {
  *(f->result) = f->entry(f->args);
  printf("Done running entry function. Switching back to caller...\n");
  switch_context(&f->context, &f->caller);
}

fiber_t *fiber_spawn(void *(*entry)(), void *args, size_t len) {
  fiber_t *self = malloc(sizeof(fiber_t));
  if (!self) {
    fprintf(stderr, "Couldn't allocate memory for new fiber context\n");
    return NULL;
  }

  // Create new stack, 16 bytes aligned
  void *stack = NULL;
  int status = posix_memalign(&stack, 16, STACK_SIZE * sizeof(uint64_t));
  if (status != 0) {
    fprintf(stderr, "Error allocating mem for stack: %d\n", status);
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
  self->len = len; // NOTE: isn't this redundant?
  // Set id
  self->id = count++;

  return self;
}

void fiber_run(fiber_t *f, void **result) {
  printf("Hello from fiber %d!\n", f->id);
  f->result = result;
  switch_context(&f->caller, &f->context);
}

////////////////////////////////////////////////////
////////////////////////////////////////////////////

void *sum(void *args) {
  printf("Hi from entry function!\n");

  int a = ((int *)args)[0];
  int b = ((int *)args)[1];

  int *result = malloc(sizeof(int));
  if (!result) {
    fprintf(stderr, "Sum: Could not allocate memory for result\n");
    return NULL;
  }

  *result = a + b;
  return result;
}

int main() {
  printf("Hello from main! About to switch context...\n");

  int args[] = {1, 2};
  int args2[] = {3, 4};
  void *result;

  fiber_t *f1 = fiber_spawn((void *)sum, args, 2);
  fiber_t *f2 = fiber_spawn((void *)sum, args2, 2);

  fiber_run(f1, &result); // blocks
  printf("Hello again from main!\n");
  printf("Result from fiber %d: %d\n", f1->id, *(int *)result);
  free(result);

  fiber_run(f2, &result); // blocks
  printf("Hello again from main!\n");
  printf("Result from fiber %d: %d\n", f2->id, *(int *)result);
  free(result);

  fiber_destroy(f1);
  fiber_destroy(f2);

  printf("Hello again from main!\n");

  return 0;
}
