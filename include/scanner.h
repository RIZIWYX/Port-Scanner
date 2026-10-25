#include <sys/socket.h>

#ifndef SCANNER_H
#define SCANNER_H

typedef struct {
  const char *host;
  int port_start;
  int port_end;
  int timeout_ms;
} scan_config;

int parse_args(int argc, char **argv, scan_config *cfg);

typedef enum {
  PORT_OPEN, PORT_CLOSED, PORT_FILTERED, PORT_ERROR
} port_state;

int resolve_host(const char *host, struct sockaddr_storage *addr,
    socklen_t *len);

port_state scan_port(const struct sockaddr_storage *addr, socklen_t len,
    int port);

const char *state_str(port_state s);

#endif
