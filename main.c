#include "fiber.h"
#include "runtime.h"
#include "scheduler.h"
#include "sync.h"
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
// void looping(void *args);
void hello(void);
void rpipe(void *args);
void wpipe(void *args);

struct args {
  int fd;
  waitgroup_t *wg;
};

int entry() {
  printf("Hello from main! About to spawn fibers\n");

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    printf("pipe error\n");
  }

  waitgroup_t wgread = wg_make();
  waitgroup_t wgwrite = wg_make();

  struct args readargs = {.fd = pipefd[0], .wg = &wgread};
  struct args writeargs = {.fd = pipefd[1], .wg = &wgwrite};

  wg_add(&wgwrite, 1);
  wg_add(&wgread, 1);

  fiber_t *f1 = fiber_spawn((void *)wpipe, (void *)&writeargs, 1, NULL);
  fiber_t *f2 = fiber_spawn((void *)rpipe, (void *)&readargs, 1, NULL);

  printf("Done spawning fibers\n\n");

  wg_wait(&wgwrite);
  printf("Done waiting for fiber %d\n", f1->id);
  printf("\n");

  wg_wait(&wgread);
  printf("Done waiting for fiber %d\n", f2->id);
  printf("\n");

  free(f1);
  free(f2);

  printf("Hello again from main!\n");

  return 0;
}

int main() {
  sched_init();
  sched_start((void *)entry);
  return 0;
}

void hello() { printf("hello world\n"); }

// void looping(void *args) {
//   int a = ((int *)args)[0];
//   int b = ((int *)args)[1];

//   for (int i = a; i < b; i++) {
//     if (i == b - 2) {
//       fiber_t *f2 = fiber_spawn((void *)hello, NULL, 0, NULL);
//       fiber_await(f2);
//       free(f2);
//     }
//     printf("%d\n", i);
//     fiber_yield();
//   }
// }

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

void rpipe(void *args) {
  struct args *myargs = (struct args *)args;
  int fd = myargs->fd;
  waitgroup_t *wg = myargs->wg;

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
      wg_done(wg);
      return;
    }
    count += n;
    if (count == 3) {
      break;
    }
  }
  printf("read from fd %d: %c%c\n", fd, buf[0], buf[1]);
  wg_done(wg);
}

void wpipe(void *args) {
  struct args *myargs = (struct args *)args;
  int fd = myargs->fd;
  waitgroup_t *wg = myargs->wg;

  const char *str = "hi";
  int n = fiber_write(fd, (void *)str, 3);
  if (n == -1) {
    printf("fiber_write error, %d\n", errno);
    return;
  }
  printf("wrote to fd %d: %s\n", fd, str);
  wg_done(wg);
}
