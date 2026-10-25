#ifndef SCANNER_H
#define SCANNER_H

typedef struct {
  const char *host;
  int port_start;
  int port_end;
  int timeout_ms;
} scan_config;

int parse_args(int argc, char **argv, scan_config *cfg);

#endif
