#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "runtime.h"
#include "scheduler.h"
#include "sync.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFSIZE 1048576
typedef uint64_t u64;
typedef size_t usize;

const char *fizz = "Fizz\n";
const char *buzz = "Buzz\n";
const char *fizzbuzz = "FizzBuzz\n";
const usize fizz_size = 5;
const usize buzz_size = 5;
const usize fizzbuzz_size = 9;

typedef struct {
  usize len;
  char *data;
  int ready;
} Buffer;

static char buf1[BUFSIZE];
static char buf2[BUFSIZE];
static Buffer buffers[2] = {
    {.len = 0, .data = buf1, .ready = 0},
    {.len = 0, .data = buf2, .ready = 0},
};

usize digits(u64 n) {
  // clang-format off
  if (n < 10ULL) return 1;
  if (n < 100ULL) return 2;
  if (n < 1000ULL) return 3;
  if (n < 10000ULL) return 4;
  if (n < 100000ULL) return 5;
  if (n < 1000000ULL) return 6;
  if (n < 10000000ULL) return 7;
  if (n < 100000000ULL) return 8;
  if (n < 1000000000ULL) return 9;
  if (n < 10000000000ULL) return 10;
  if (n < 100000000000ULL) return 11;
  if (n < 1000000000000ULL) return 12;
  if (n < 10000000000000ULL) return 13;
  if (n < 100000000000000ULL) return 14;
  if (n < 1000000000000000ULL) return 15;
  if (n < 10000000000000000ULL) return 16;
  if (n < 100000000000000000ULL) return 17;
  if (n < 1000000000000000000ULL) return 18;
  if (n < 10000000000000000000ULL) return 19;
  return 20;
  // clang-format on
}

static inline usize itoa_u64(char *buf, u64 n) {
  char tmp[20];
  int len = 0;
  do {
    tmp[len++] = '0' + (n % 10);
    n /= 10;
  } while (n);
  for (int i = len - 1; i >= 0; i--)
    buf[len - 1 - i] = tmp[i];
  buf[len] = '\n';
  return len + 1;
}

char *msg = "";

void buffer(void *args) {
  channel_t **myargs = (channel_t **)args;
  channel_t *buf_ready = myargs[0];
  channel_t *buf_flushed = myargs[1];

  usize bufindex = 0;
  Buffer *buf;

  u64 end = UINT64_MAX;

  char digits_buf[21];

  usize num_digits;
  usize pattern_index = 0;
  for (u64 i = 0; i <= end; i++) {
    buf = &buffers[bufindex];
    switch (pattern_index) {
    case 0:
      // fizzbuzz
      memcpy(buf->data + buf->len, fizzbuzz, fizzbuzz_size);
      buf->len += fizzbuzz_size;
      break;
    case 3:
    case 6:
    case 9:
    case 12:
      // fizz
      memcpy(buf->data + buf->len, fizz, fizz_size);
      buf->len += fizz_size;
      break;
    case 5:
    case 10:
      // buzz
      memcpy(buf->data + buf->len, buzz, buzz_size);
      buf->len += buzz_size;
      break;
    default:
      // number
      num_digits = itoa_u64(digits_buf, i);
      memcpy(buf->data + buf->len, digits_buf, num_digits);
      buf->len += num_digits;
      break;
    }
    if (++pattern_index > 14) {
      pattern_index = 0;
    }

    void *result = NULL;
    char *data = msg;
    if (BUFSIZE - buf->len < 20) {
      buf->ready = 1;
      chan_send(buf_ready, (void *)data);

      bufindex = 1 - bufindex;
      buf = &buffers[bufindex];
      chan_recv(buf_flushed, &result);
    }
  }
}

void fizzbuzzfiber() {
  channel_t *buf_ready = chan_make();
  channel_t *buf_flushed = chan_make();

  channel_t *args[2] = {buf_ready, buf_flushed};

  fiber_spawn(buffer, (void *)args);

  usize bufindex = 0;
  Buffer *buf;

  void *result = NULL;
  char *data = msg;
  while (1) {
    buf = &buffers[bufindex];
    chan_recv(buf_ready, &result);

    write(STDOUT_FILENO, buf->data, buf->len);

    buf->len = 0;
    buf->ready = 0;
    chan_send(buf_flushed, (void *)data);
    bufindex = 1 - bufindex;
  }
}

int main() {
  sched_init();
  sched_start(fizzbuzzfiber, 0, NULL);
  return 0;
}
