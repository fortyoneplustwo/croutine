#include "runtime.h"
#include <asm-generic/errno-base.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

void *sum(void *args);
void looping(void *args);
void hello(void);

void read_from_pipe() {
  char buf[3];
  buf[2] = '\0';
  int pipefd[2];
  int res;
  res = pipe(pipefd);
  if (res == -1) {
    printf("pipe error\n");
    return;
  }
  res = write(pipefd[1], "hi", 2);
  if (res == -1) {
    printf("write error\n");
    return;
  }
  res = fiber_read(pipefd[0], &buf, 2);
  if (res == -1) {
    printf("read error\n");
    return;
  }
  printf("read %d bytes\ncontents of buf are: %c%c\n", res, buf[0], buf[1]);
}

int main() {
  printf("Hello from main! About to spawn fibers\n");

  sched_init();

  int args1[] = {1, 6};
  int args2[] = {6, 16};

  int *r1;
  int *r2;

  fiber_t *f1 = fiber_spawn((void *)looping, (void *)args1, 2, NULL);
  fiber_t *f2 = fiber_spawn((void *)looping, (void *)args2, 2, NULL);

  printf("Done spawning fibers\n\n");

  fiber_await(f1);
  printf("\nReturned from await fiber %d\n", f1->id);
  // printf("Result from fiber %d: %d\n", f1->id, *((int*)r1));
  printf("\n");

  fiber_await(f2);
  printf("\nReturned from await fiber %d\n", f2->id);
  // printf("Result from fiber %d: %d\n", f2->id, *r2);
  printf("\n");

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
      fiber_t *f2 = fiber_spawn((void *)read_from_pipe, NULL, 0, NULL);
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
