#ifndef NET_H
#define NET_H

#include <sys/socket.h>

typedef enum {
  PORT_OPEN, PORT_CLOSED, PORT_FILTERED, PORT_ERROR
} port_state;

int resolve_host(const char *host, struct sockaddr_storage *addr,
    socklen_t *len);

port_state scan_port(const struct sockaddr_storage *addr, socklen_t len,
    int port, int timeout_ms);

const char *state_str(port_state s);

#endif
