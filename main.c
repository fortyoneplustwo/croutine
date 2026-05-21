#include "runtime.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

void *sum(void *args);
void looping(void *args);
void hello(void);

int main() {
  printf("Hello from main! About to spawn fibers\n");

  sched_init();

  int args1[] = {1, 6};
  int args2[] = {6, 16};

  // int *r1;
  // int *r2;

  fiber_t *f1 = fiber_spawn((void *)looping, (void *)args1, 2, NULL);
  fiber_t *f2 = fiber_spawn((void *)looping, (void *)args2, 2, NULL);

  printf("Done spawning fibers\n\n");

  fiber_await(f1);
  printf("\nReturned from await fiber %d\n\n", f1->id);
  // printf("Result from fiber %d: %d\n", f1->id, *r1);
  fiber_await(f2);
  printf("\nReturned from await fiber %d\n\n", f2->id);
  // printf("Result from fiber %d: %d\n", f2->id, *r2);

  free(f1);
  free(f2);

  printf("Hello again from main!\n");

  return 0;
}

void hello() { printf("hello world\n"); }

void looping(void *args) {
  int a = ((int *)args)[0];
  int b = ((int *)args)[1];

  for (int i = a; i < b; i++) {
    if (i == b - 2) {
      fiber_t *f2 = fiber_spawn((void *)hello, NULL, 0, NULL);
      fiber_await(f2);
      free(f2);
    }
    printf("%d\n", i);
    fiber_yield();
  }
}

void *sum(void *args) {
  printf("Hi from entry function!\n");

  int a = ((int *)args)[0];
  int b = ((int *)args)[1];

  int *result = (int *)malloc(sizeof(int));
  if (result == NULL) {
    fprintf(stderr, "Sum: Could not allocate memory for result\n");
    return NULL;
  }

  *result = a + b;
  return result;
}
