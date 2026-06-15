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

void read_from_pipe(void *args);
void write_to_pipe(void *args);

struct msg {
  char *str;
  int len;
};

struct args {
  int fd;
  channel_t *ch;
};

void entry() {
  printf("Hello from main (entry)! About to spawn fibers\n");

  // Create pipe
  int pipefd[2];
  if (pipe(pipefd) == -1) { 
    printf("pipe error\n"); 
  }

  // Create a channel for fibers to communicate with main
  channel_t ch = chan_make(); 

  // Create args, passing in pipe ends and channel
  struct args readargs = {.fd = pipefd[0], .ch = &ch};
  struct args writeargs = {.fd = pipefd[1], .ch = &ch};

  // Spawn fibers. Each fiber will send a msg to us on success.
  fiber_spawn((void *)write_to_pipe, (void *)&writeargs);
  fiber_spawn((void *)read_from_pipe, (void *)&readargs);

  // Receive messages from the channel.
  // Blocks until a msg is received.
  void *result;
  while (chan_recv(&ch, (void **)&result) != -1) {
    printf("Msg received: %s\n", (char *)result);
    free(result);
  }

  printf("Channel closed\n");
  close(pipefd[0]);
  close(pipefd[1]);
}



int main() {
  sched_init(); // Initialize runtime
  sched_start((void *)entry); // Enter runtime with main function "entry"
  return 0;
}

void read_from_pipe(void *args) {
  // Destructure args
  struct args *myargs = (struct args *)args;
  int fd = myargs->fd;
  channel_t *ch = myargs->ch;

  // Attempt to read from pipe
  char buf[3];
  int count = 0;
  while (1) {
    int n = fiber_read(fd, &buf, 3);
    if (n == -1) {
      printf("fiber_read error: %d\n", errno);
      chan_close(ch);
      return;
    }
    count += n;
    if (count == 3) {
      break;
    }
  }

  // Send success message to main via channel
  char *msg = (char *)malloc(sizeof(char) * 32);
  sprintf(msg, "Read from pipe: %s\n", buf);
  chan_send(ch, (void *)msg);
  chan_close(ch);
}

void write_to_pipe(void *args) {
  // destructure arguments
  struct args *myargs = (struct args *)args;
  int fd = myargs->fd;
  channel_t *ch = myargs->ch;

  // Write "hi" to pipe
  // Close channel if error
  const char *str = "hi";
  int n = fiber_write(fd, (void *)str, 3);
  if (n == -1) {
    printf("fiber_write error, %d\n", errno);
    chan_close(ch);
    return;
  }

  // Send success msg to main via channel
  char *msg = malloc(sizeof(char) * 32);
  sprintf(msg, "Wrote to pipe: hi\n");
  chan_send(ch, (void *)msg);
}
