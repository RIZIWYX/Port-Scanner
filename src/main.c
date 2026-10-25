#include <stdio.h>
#include "scanner.h"

int main(int argc, char **argv) {
  scan_config cfg;
  if (parse_args(argc, argv, &cfg) < 0) {
    return 1;
  }
  struct sockaddr_storage addr;
  socklen_t len;
  if (resolve_host(cfg.host, &addr, &len) < 0) {
    return 1;
  }
  printf("Scan de %s, ports %d-%d\n\n", cfg.host, cfg.port_start, cfg.port_end);
  printf("%-8s %s\n", "PORT", "ETAT");
  int open_count = 0;
  for (int p = cfg.port_start; p <= cfg.port_end; p++) {
    port_state st = scan_port(&addr, len, p);
    if (st == PORT_OPEN) {
      printf("%-8d %s\n", p, state_str(st));
      open_count++;
    }
  }
  printf("\n%d port(s) ouvert(s)\n", open_count);
  return 0;
}
