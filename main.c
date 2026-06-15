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
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void rpipe(void *args);
void wpipe(void *args);

struct args {
  int fd;
  channel_t *ch;
  channel_t *done;
};

int entry() {
  int err;
  int p[2];

  err = pipe(p);
  if (err) {
    perror("coudln't create pipe\n");
    exit(errno);
  }

  channel_t done = chan_make();
  channel_t msg = chan_make();

  struct args rargs = {.fd = p[0], .ch = &msg, .done = &done};
  struct args wargs = {.fd = p[1], .ch = &msg, .done = &done};

  fiber_spawn(wpipe, &wargs);
  fiber_spawn(rpipe, &rargs);

  char *result;
  while (1) {
    int closed = chan_recv(&msg, (void *)&result);
    if (closed) {
      break;
    }
    printf("Msg received: %s\n", result);
    free(result);
  }
  return 0;
}

int main() {
  sched_init();
  sched_start((void *)entry);
  return 0;
}

void wpipe(void *args) {
  struct args *myargs = args;
  int err;
  const char *str = "hi";
  write(myargs->fd, str, strlen(str) + 1);
  close(myargs->fd);
  char *msg = malloc(strlen(str) + 1);
  if (!msg) {
    perror("wpipe: couldn't allocate memory for msg\n");
    exit(1);
  }
  strcpy(msg, str);
  chan_send(myargs->ch, msg);
  chan_send(myargs->done, msg);
}

void rpipe(void *args) {
  struct args *myargs = args;
  void *result;
  char *buf;
  int err;
  int n = 0;
  buf = malloc(sizeof(*buf) * 3);
  if (!buf) {
    perror("rpipe: couldn't allocate mem for buf\n");
    exit(1);
  }
  chan_recv(myargs->done, &result);
  while ((n = read(myargs->fd, buf + n, 3)) > 0)
    ;
  close(myargs->fd);
  if (n == -1) {
    perror("rpipe: error reading from fd\n");
    exit(1);
  }
  chan_send(myargs->ch, buf);
  chan_close(myargs->ch);
}
