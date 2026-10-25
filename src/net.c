#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include "net.h"

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

/*
 * set_port : ecrit un numero de port dans une adresse IPv4 ou IPv6.
 *
 * Parametres :
 *   addr : adresse a modifier ; sa famille (ss_family) doit etre AF_INET
 *          ou AF_INET6.
 *   port : port en ordre de la machine, converti en ordre reseau par
 *          htons avant ecriture.
 */
static void set_port(struct sockaddr_storage *addr, int port) {
  if (addr->ss_family == AF_INET) {
    ((struct sockaddr_in *) addr)->sin_port = htons(port);
  } else {
    ((struct sockaddr_in6 *) addr)->sin6_port = htons(port);
  }
}

port_state scan_port(const struct sockaddr_storage *base, socklen_t len,
    int port, int timeout_ms) {
  struct sockaddr_storage addr = *base;
  set_port(&addr, port);
  int fd = socket(addr.ss_family, SOCK_STREAM, 0);
  if (fd < 0) {
    return PORT_ERROR;
  }
  /* 1. Passage en non bloquant */
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(fd);
    return PORT_ERROR;
  }
  /* 2. connect rend la main immediatement */
  port_state st;
  if (connect(fd, (struct sockaddr *) &addr, len) == 0) {
    st = PORT_OPEN;                       /* connexion locale instantanee */
  } else if (errno == ECONNREFUSED) {
    st = PORT_CLOSED;
  } else if (errno != EINPROGRESS) {
    st = PORT_ERROR;
  } else {
    /* 3. Attente bornee */
    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc == 0) {
      st = PORT_FILTERED;               /* aucune reponse dans le delai */
    } else if (rc < 0) {
      st = PORT_ERROR;
    } else {
      /* 4. Resultat reel de la connexion */
      int err = 0;
      socklen_t elen = sizeof err;
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0) {
        st = PORT_ERROR;
      } else if (err == 0) {
        st = PORT_OPEN;
      } else if (err == ECONNREFUSED) {
        st = PORT_CLOSED;
      } else if (err == EHOSTUNREACH || err == ENETUNREACH
          || err == ETIMEDOUT) {
        st = PORT_FILTERED;
      } else {
        st = PORT_ERROR;
      }
    }
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
