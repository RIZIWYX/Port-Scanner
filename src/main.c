#include <stdio.h>
#include "scanner.h"

int main(int argc, char **argv) {
  scan_config cfg;
  if (parse_args(argc, argv, &cfg) < 0) {
    return 1;
  }
  printf("Cible : %s, ports %d-%d, timeout %d ms\n",
      cfg.host, cfg.port_start, cfg.port_end, cfg.timeout_ms);
  return 0;
}
