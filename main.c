#include "runtime.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

void *sum(void *args);
void looping(void *args);
void hello(void);
void rpipe(void *args);
void wpipe(void *args);

void rpipe(void *args) {
  int fd = *((int *)args);
  char buf[3];
  int count = 0;
  while (1) {
    int n = fiber_read(fd, &buf, 3);
    if (n == -1) {
      if (errno == EBADF || errno == EINTR) {
        printf("not ready\n");
        fiber_yield();
        continue;
      }
      printf("fiber_read error: %d\n", errno);
      return;
    }
    count += n;
    if (count == 3) {
      break;
    }
  }
  printf("read from fd %d: %c%c\n", fd, buf[0], buf[1]);
}

void wpipe(void *args) {
  int fd = *((int *)args);
  const char *str = "hi";
  int n = fiber_write(fd, (void *)str, 3);
  if (n == -1) {
    printf("fiber_write error, %d\n", errno);
    return;
  }
  printf("wrote to fd %d: %s\n", fd, str);
}

int main() {
  printf("Hello from main! About to spawn fibers\n");

  sched_init();

  // int args1[] = {1, 6};
  // int args2[] = {6, 16};

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    printf("pipe error\n");
  }

  int *r1;
  int *r2;

  // Not working when the things are reversed. why?
  fiber_t *f2 = fiber_spawn((void *)rpipe, (void *)&pipefd[0], 1, NULL);
  fiber_t *f1 = fiber_spawn((void *)wpipe, (void *)&pipefd[1], 1, NULL);

  printf("Done spawning fibers\n\n");

  fiber_yield();

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
      fiber_t *f2 = fiber_spawn((void *)hello, NULL, 0, NULL);
      fiber_await(f2);
      free(f2);
    }
    printf("%d\n", i);
    fiber_yield();
  }
}

void *sum(void *args) {
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
