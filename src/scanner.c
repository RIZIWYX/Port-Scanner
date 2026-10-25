#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scanner.h"

static void usage(const char *prog) {
  fprintf(stderr, "Usage : %s <hote> <port_debut> <port_fin> [-t timeout_ms]\n",
      prog);
}

static int parse_int(const char *s, int min, int max, int *out) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*s == '\0' || *end != '\0' || v < min || v > max) {
    return -1;
  }
  *out = (int) v;
  return 0;
}

int parse_args(int argc, char **argv, scan_config *cfg) {
  if (argc != 4 && argc != 6) {
    usage(argv[0]);
    return -1;
  }
  cfg->host = argv[1];
  cfg->timeout_ms = 500;
  if (parse_int(argv[2], 1, 65535, &cfg->port_start) < 0
      || parse_int(argv[3], 1, 65535, &cfg->port_end) < 0
      || cfg->port_start > cfg->port_end) {
    fprintf(stderr, "Plage de ports invalide (1-65535, debut <= fin)\n");
    return -1;
  }
  if (argc == 6) {
    if (strcmp(argv[4], "-t") != 0
        || parse_int(argv[5], 10, 10000, &cfg->timeout_ms) < 0) {
      usage(argv[0]);
      return -1;
    }
  }
  return 0;
}
