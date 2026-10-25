#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "scanner.h"

int resolve_host(const char *host, struct sockaddr_storage *addr,
    socklen_t *len) {
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;      /* IPv4 ou IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  int rc = getaddrinfo(host, NULL, &hints, &res);
  if (rc != 0) {
    fprintf(stderr, "Resolution de %s impossible : %s\n", host,
        gai_strerror(rc));
    return -1;
  }
  memcpy(addr, res->ai_addr, res->ai_addrlen);
  *len = res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

static void set_port(struct sockaddr_storage *addr, int port) {
  if (addr->ss_family == AF_INET) {
    ((struct sockaddr_in *) addr)->sin_port = htons(port);
  } else {
    ((struct sockaddr_in6 *) addr)->sin6_port = htons(port);
  }
}

port_state scan_port(const struct sockaddr_storage *base, socklen_t len,
    int port) {
  struct sockaddr_storage addr = *base;
  set_port(&addr, port);
  int fd = socket(addr.ss_family, SOCK_STREAM, 0);
  if (fd < 0) {
    return PORT_ERROR;
  }
  port_state st;
  if (connect(fd, (struct sockaddr *) &addr, len) == 0) {
    st = PORT_OPEN;
  } else if (errno == ECONNREFUSED) {
    st = PORT_CLOSED;
  } else if (errno == ETIMEDOUT || errno == EHOSTUNREACH) {
    st = PORT_FILTERED;
  } else {
    st = PORT_ERROR;
  }
  close(fd);
  return st;
}

const char *state_str(port_state s) {
  switch (s) {
    case PORT_OPEN:
      return "ouvert";
    case PORT_CLOSED:
      return "ferme";
    case PORT_FILTERED:
      return "filtre";
    default:
      return "erreur";
  }
}
