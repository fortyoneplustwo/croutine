# croutine

A single-threaded user-space concurrency runtime library for C, inspired by
goroutines.

Target architecture: x86_64 Linux.

_This project is for educational purposes and is a work in progress. Nearly all
functions are exported globally for ease of testing._

## Requirements

- x86_64 Linux
- GCC (or a compiler supporting the same inline assembly / calling conventions)
- `make`

## Install

A `Makefile` is provided that will output an executable named `a.out`. To build
it, `cd` into the project's root directory and run `make`.

```bash
make
```

## Usage

The following example program prints `"Hello, world!"` using 2 fibers. One fiber
prints `"Hello"` while the other prints `", world!"`. A wait group is used to
synchronize between them so the messages are always printed in order.

```c
#include "runtime.h" // Import runtime functions
#include <stdio.h>

void task_hello(void *args) {
  printf("Hello");
}

void task_main(void *args) {
  // Initialize a wait group to handle
  // synchronization between fibers
  waitgroup_t *wg = wg_make();

  // Spawn a fiber to run task_hello
  wg_spawn(wg, task_hello, NULL);

  // Wait for task_hello to finish
  wg_wait(wg);

  printf(", world!\n");

  // Free our wait group
  wg_free(wg);
}

int main() {
  sched_init();

  // Start the runtime, passing it a main task
  // with argc and argv.
  // In this case, it takes no arguments.
  sched_start(task_main, 0, NULL);

  // If reached here, we have exited the runtime
  return 0;
}
```

## Implementation

The entire runtime runs on a single kernel thread.

### Fibers

Spawned with an 8MB stack allocated on the heap. Stack overflow handling is
delegated to the kernel.

### Context switch

Implemented in assembly. Saves and restores callee-saved registers following the
System V ABI specification.

### Scheduler

Holds a queue of fibers that are ready to run. The scheduler always dequeues the
next fiber and runs it. It also manages fiber lifetime, automatically freeing
resources when a fiber finishes executing its task.

### Channels

Modeled after Go's unbuffered channels, except that it returns error values
rather than panicking.

### Wait groups

Modeled after Go's wait groups, except that it returns error values rather than
panicking.

### Netpoller

The netpoller intermittently polls the kernel for I/O using `epoll` in
edge-triggered mode and dispatches events to fibers that are ready. It runs on a
dedicated fiber that is always on the run queue, so the frequency of I/O polling
is determined by the number of fibers on the run queue.

If the netpoller is the only fiber on the run queue, it puts the entire program
to sleep until the kernel wakes it to process events that have arrived.

### Asynchronous I/O

Fiber-blocking I/O functions are provided that closely follow their glibc
equivalents: `read`, `write`, `accept`, and `connect`.

## API

```c
/**
 *Scheduler
 */

// Initialize the runtime
int sched_init(void);

// Enter the run time with a main function with arguments and arguments count
int sched_start(void (*main)(int, char**), int argc, char **argv);

/**
 * Fibers
 */

// Spawn a fiber passing it a function with arguments
void fiber_spawn(void (*entry)(), void *args);

// Pause execution of the calling fiber and resume at a later time
// determined by the scheduler.
void fiber_yield(void);

/**
 * Channels
 */

// Allocate a channel on the heap.
channel_t *chan_make(void);

// Send data on a channel, blocking the calling fiber until there is a receiver.
// Returns 0 on success or -1 otherwise.
int chan_send(channel_t *ch, void *data);

// Receive data from a channel, block the calling fiber until there is a sender.
// Returns 0 on success or -1 otherwise.
int chan_recv(channel_t *ch, void **result);

// Close the channel.
// Sending on a closed channel will fail.
// Receiving on a closed and drained channel will fail.
void chan_close(channel_t *ch);

// Free the memory allocated for a channel.
void freechan(channel_t *ch);

/**
 * Wait groups
 */

// Allocate a wait group on the heap.
waitgroup_t *wg_make();

// Increment the waitgroup's count by n.
// Returns -1 if the count goes negative or 0 otherwise.
int wg_add(waitgroup_t *wg, int n);

// Decrement the waitgroup's count by 1;
// If count reaches 0, all blocked fibers are scheduled to run.
void wg_done(waitgroup_t *wg);

// Blocks the calling fiber until the waitgroup's count reaches 0.
void wg_wait(waitgroup_t *wg);

// Spawn a fiber with a function and arguments,
// adding it to the waitgroup's list.
// A fiber spawned using this function will automatically
// decrement the waitgroup's count when it has completed its task.
void wg_spawn(waitgroup_t *wg, void *(*fn)(), void *args);

// Free the memory allocated for a waitgroup.
void freewg(waitgroup_t *wg);

/**
 * I/O
 */

// Read n bytes from file descriptor fd into buffer buf.
// Returns the number of bytes read on success or -1 on error.
ssize_t fiber_read(int fd, void *buf, size_t n);

// Write n bytes from buffer buf to file descriptor fd.
// Returns the number of bytes written on success or -1 on error.
ssize_t fiber_write(int fd, void *buf, size_t n);

// Accept connections on a socket descriptor sockfd.
// Store the connection info in addr and info length in addrlen.
// Returns a socket descriptor assigned for the new connection on success
// or -1 on error.
int fiber_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

// Initiate a connection on a socket descriptor sockfd 
// to the address specified by addr with size addrlen.
// Returns 0 on success or -1 on error.
int fiber_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```
## Roadmap

- Interception of standard blocking I/O calls
- Multi-threaded scheduler support
- Work stealing
- Growable stacks
