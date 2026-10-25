#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

/*
 * usage : affiche la syntaxe d'appel du programme sur stderr.
 *   prog : nom du programme, en general argv[0].
 */
static void usage(const char *prog) {
  fprintf(stderr, "Usage : %s <hote> <port_debut> <port_fin> [-t timeout_ms]\n",
      prog);
}

/*
 * parse_int : convertit une chaine en entier et verifie ses bornes.
 *
 * Parametres :
 *   s        : chaine a convertir, en base 10.
 *   min, max : bornes inclusives acceptees.
 *   out      : recoit la valeur convertie en cas de succes.
 *
 * Retour :
 *   0 si s est entierement un entier compris dans [min, max].
 *   -1 si s est vide, contient autre chose qu'un entier (ex. "80abc")
 *   ou sort des bornes ; *out n'est alors pas modifie.
 */
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
