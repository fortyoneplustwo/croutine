#include "runtime.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

void *sum(void *args);
void looping(void *args);

int main() {
  printf("Hello from main! About to spawn fibers\n");

  sched_init();

  int args1[] = {1, 6};
  int args2[] = {6, 16};

  fiber_t *f1 = fiber_spawn((void *)looping, (void *)args1, 0);
  fiber_t *f2 = fiber_spawn((void *)looping, (void *)args2, 0);

  printf("Done spawning fibers\n\n");

  fiber_await(f1);
  printf("\nReturned from await fiber %d\n\n", f1->id);
  fiber_await(f2);
  printf("\nReturned from await fiber %d\n\n", f2->id);

  free(f1);
  free(f2);

  printf("Hello again from main!\n");

  return 0;
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

void looping(void *args) {
  int a = ((int *)args)[0];
  int b = ((int *)args)[1];

  for (int i = a; i < b; i++) {
    printf("%d\n", i);
    fiber_yield();
  }
}
