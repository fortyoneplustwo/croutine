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
void looping(void *args);
void hello(void);
void *rpipe(void *args);
void wpipe(void *args);

struct args {
  int fd;
  channel_t *ch;
};

int entry() {
  printf("Hello from main! About to spawn fibers\n");

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    printf("pipe error\n");
  }

  channel_t ch = chan_make();

  struct args readargs = {.fd = pipefd[0], .ch = &ch};
  struct args writeargs = {.fd = pipefd[1], .ch = &ch};

  fiber_spawn((void *)rpipe, (void *)&readargs, 0, NULL);
  fiber_spawn((void *)wpipe, (void *)&writeargs, 0, NULL);

  printf("Done spawning fibers\n\n");

  void *result;
  while (chan_recv(&ch, (void **)&result) != -1) {
    printf("received: %d\n", *(int *)result);
    free(result);
  }
  printf("channel closed\n");

  printf("\nHello again from main!\n");

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
  printf("inside sum\n");
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

void *rpipe(void *args) {
  struct args *myargs = (struct args *)args;
  int fd = myargs->fd;
  channel_t *ch = myargs->ch;

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
      chan_close(ch);
      return NULL;
    }
    count += n;
    if (count == 3) {
      break;
    }
  }
  printf("read from fd %d: %c%c\n", fd, buf[0], buf[1]);
  int *ret = (int *)malloc(sizeof(int));
  *ret = 10;
  chan_send(ch, (void *)ret);
  chan_close(ch);
  return ret;
}

void wpipe(void *args) {
  struct args *myargs = (struct args *)args;
  int fd = myargs->fd;
  channel_t *ch = myargs->ch;

  const char *str = "hi";
  int n = fiber_write(fd, (void *)str, 3);
  if (n == -1) {
    printf("fiber_write error, %d\n", errno);
    chan_close(ch);
    return;
  }
  printf("wrote to fd %d: %s\n", fd, str);
  int *ret = (int *)malloc(sizeof(int));
  *ret = 8;
  chan_send(ch, (void *)ret);
}
