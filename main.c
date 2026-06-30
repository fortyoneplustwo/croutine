#include "fiber.h"
#include "io.h"
#include "netpoller.h"
#include "runtime.h"
#include "scheduler.h"
#include "sync.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

void rpipe(void *args);
void wpipe(void *args);

struct args {
  int fd;
  channel_t *ch;
  channel_t *done;
};

struct handlerargs {
  struct sockaddr *clientaddr;
  socklen_t clientaddr_len;
  int connfd;
};

void handler(void *args) {
  int connfd = ((struct handlerargs *)args)->connfd;
  int rsockfd;

  while (1) {
    int n;
    char buf[128];

    n = 0;
    memset(buf, 0, 128);
    n = fiber_read(connfd, buf, 127);
    if (n == -1) {
      fprintf(stdout, "failed to read\n");
      break;
    }
    if (n == 0) {
      fprintf(stdout, "client %d has closed connection\n", sched->running->id);
      break;
    }
    // write(1, buf, n);

    int err;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *raddr;

    err = getaddrinfo(NULL, "8080", &hints, &raddr);
    if (err) {
      perror("failed to get raddr info");
      return;
    }

    struct addrinfo *p;
    char s[INET_ADDRSTRLEN];
    for (p = raddr; p != NULL; p = p->ai_next) {
      rsockfd =
          socket(raddr->ai_family, raddr->ai_socktype, raddr->ai_protocol);
      if (rsockfd == -1) {
        perror("failed to create rsocket");
        continue;
      }
      inet_ntop(p->ai_family, (void *)&((struct sockaddr_in *)p->ai_addr)->sin_addr, s,
                sizeof s);
      // printf("client: attempting connection to %s\n", s);
      // printf("port %d\n", ntohs(((struct sockaddr_in *)p->ai_addr)->sin_port));
      err = fiber_connect(rsockfd, raddr->ai_addr, raddr->ai_addrlen);
      if (err) {
        perror("failed to connect to peer");
        closefd(rsockfd);
        continue;
      }
      break;
    }

    freeaddrinfo(raddr);

    if (p == NULL) {
      printf("failed to connect to peer\n");
      return;
    }

    n = fiber_write(rsockfd, buf, n);
    if (n == -1) {
      fprintf(stdout, "failed to write to peer, errno: %d\n", errno);
      break;
    }

    n = fiber_read(rsockfd, buf, 127);
    if (n == -1) {
      fprintf(stdout, "failed to read from rsockfd\n");
      break;
    }
    if (n == 0) {
      fprintf(stdout, "r has closed connection\n");
      break;
    }
    // write(1, buf, n);

    n = fiber_write(connfd, buf, n);
    if (n == -1) {
      fprintf(stdout, "failed to write, errno: %d\n", errno);
      break;
    }
  }

  int err = closefd(connfd);
  if (err) {
    fprintf(stdout, "ERROR closing connfd: %d\n", errno);
  }
  err = closefd(rsockfd);
  if (err) {
    fprintf(stdout, "ERROR closing rsockfd: %d\n", errno);
  }
  free(((struct handlerargs *)args)->clientaddr);
  free(args);
}

void server() {
  int err;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *serverinfo;

  err = getaddrinfo(NULL, "0", &hints, &serverinfo);
  if (err) {
    perror("failed to get addr info");
    return;
  }

  int socketfd =
      socket(serverinfo->ai_family, serverinfo->ai_socktype | SOCK_NONBLOCK, 0);
  if (socketfd == -1) {
    perror("failed to get socket");
    return;
  }

  err = bind(socketfd, serverinfo->ai_addr, serverinfo->ai_addrlen);
  if (err) {
    perror("failed to bind to socket");
    close(socketfd);
    return;
  }

  struct sockaddr_in inaddr;
  socklen_t inaddr_len = sizeof(inaddr);
  getsockname(socketfd, (struct sockaddr *)&inaddr, &inaddr_len);
  printf("listening on port %d\n", ntohs(inaddr.sin_port));

  freeaddrinfo(serverinfo);

  int backlog = 10;
  err = listen(socketfd, backlog);
  if (err) {
    perror("failed to listen");
    close(socketfd);
    return;
  }

  int epfd = epoll_create(1);
  if (epfd == -1) {
    perror("failed to create epoll instance");
    close(socketfd);
    return;
  }
  struct epoll_event ev = {.data.fd = socketfd, .events = EPOLLOUT | EPOLLIN};
  err = epoll_ctl(epfd, EPOLL_CTL_ADD, socketfd, &ev);
  if (err) {
    perror("failed to set epoll config");
    close(socketfd);
    return;
  }

  while (1) {
    struct sockaddr *clientaddr = (struct sockaddr *)malloc(sizeof *clientaddr);
    if (!clientaddr) {
      fprintf(stderr, "failed to allocate mem for clientaddr\n");
      continue;
    }
    socklen_t clientaddr_len;
    int connfd = fiber_accept(socketfd, clientaddr, &clientaddr_len);
    if (connfd == -1) {
      perror("failed to accept connection");
      continue;
    }

    struct handlerargs *hargs = malloc(sizeof *hargs);
    if (!hargs) {
      fprintf(stderr, "failed to allocate mem for handler args\n");
      free(clientaddr);
      continue;
    }
    *hargs = (struct handlerargs){.clientaddr = clientaddr,
                                  .clientaddr_len = clientaddr_len,
                                  .connfd = connfd};
    fiber_spawn(handler, (void *)hargs);
  }
}

void entry() {
  int err;
  int p[2];

  err = pipe(p);
  if (err) {
    perror("coudln't create pipe\n");
    exit(errno);
  }

  channel_t *done = chan_make();
  channel_t *msg = chan_make();

  struct args rargs = {.fd = p[0], .ch = msg, .done = done};
  struct args wargs = {.fd = p[1], .ch = msg, .done = done};

  fiber_spawn(wpipe, &wargs);
  fiber_spawn(rpipe, &rargs);

  char *result;
  while (1) {
    int closed = chan_recv(msg, (void *)&result);
    if (closed) {
      break;
    }
    printf("Msg received: %s\n", result);
    free(result);
  }
}

int main() {
  sched_init();
  sched_start(server, 0, NULL);
  return 0;
}

void wpipe(void *args) {
  struct args *myargs = args;
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
