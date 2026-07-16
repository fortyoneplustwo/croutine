#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
  int err;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *serverinfo;

  err = getaddrinfo(NULL, "8000", &hints, &serverinfo);
  if (err) {
    perror("failed to get addr info");
    return errno;
  }

  int socketfd = socket(serverinfo->ai_family, serverinfo->ai_socktype, 0);
  if (socketfd == -1) {
    perror("failed to get socket");
    return errno;
  }

  err = bind(socketfd, serverinfo->ai_addr, serverinfo->ai_addrlen);
  if (err) {
    perror("failed to bind to socket");
    close(socketfd);
    return errno;
  }

  freeaddrinfo(serverinfo);

  int backlog = 10;
  err = listen(socketfd, backlog);
  if (err) {
    perror("failed to listen");
    close(socketfd);
    return errno;
  }

  int epfd = epoll_create(1);
  if (epfd == -1) {
    perror("failed to create epoll instance");
    close(socketfd);
    return errno;
  }
  struct epoll_event ev = {.data.fd = socketfd, .events = EPOLLOUT | EPOLLIN};
  err = epoll_ctl(epfd, EPOLL_CTL_ADD, socketfd, &ev);
  if (err) {
    perror("failed to set epoll config");
    close(socketfd);
    return errno;
  }

  while (1) {
    printf("going to sleep\n");
    int readyfds = epoll_wait(epfd, &ev, 1, -1);
    if (readyfds == -1) {
      perror("failed to epoll wait");
      close(socketfd);
      return errno;
    }

    if (fork() == 0) { // create a new process
      break;
    }
  }

  struct sockaddr clientinfo;
  socklen_t clientinfo_len;
  int connfd = accept(socketfd, &clientinfo, &clientinfo_len);
  if (connfd == -1) {
    perror("could not accept connection");
    close(socketfd);
    return errno;
  }

  close(socketfd); // don't need this anymore

  while (1) {
    int n, msg_len;
    char buf[1024];

    n = 0;
    memset(buf, 0, 1024);
    n = read(connfd, buf, 1023);
    if (n == -1) {
      fprintf(stdout, "failed to read\n");
      break;
    }
    printf("read: %s", buf);

    n = write(connfd, "hello\n", 6 + 1);
    if (n == -1) {
      fprintf(stdout, "failed to write\n");
      break;
    }
  }

  close(socketfd);
  return 0;
}
