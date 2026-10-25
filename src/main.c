#include <stdio.h>
#include <arpa/inet.h>
#include "cli.h"
#include "net.h"
#include "guard.h"
#include "banner.h"
#include "service.h"
#include <unistd.h>

/*
 * main : scanne une plage de ports TCP d'une cible et affiche les ports
 * ouverts avec le service identifie et sa version, puis le nombre de ports ouverts et filtres.
 *
 * Par securite, une cible hors des plages locales et privees est refusee,
 * sauf si l'option --allow-public est donnee.
 *
 * Retour : 0 si le scan a eu lieu, 1 si les arguments sont invalides ou
 * si l'hote ne peut pas etre resolu, 2 si la cible est refusee par le
 * garde-fou.
 */
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
  char ip[INET6_ADDRSTRLEN];
  addr_to_str(&addr, ip, sizeof ip);
  if (!is_private_target(&addr)) {
    if (!cfg.allow_public) {
      fprintf(stderr,
          "Refus : %s (%s) n'est pas une adresse locale ou privee.\n"
          "Ne scannez que des machines dont vous etes proprietaire ou que "
          "vous avez l'autorisation de tester.\n"
          "Si c'est le cas, relancez avec --allow-public.\n",
          cfg.host, ip);
      return 2;
    }
    fprintf(stderr, "Attention : cible publique %s, scan autorise par "
        "--allow-public.\n\n", ip);
  }
  printf("Scan de %s (%s), ports %d-%d\n\n", cfg.host, ip, cfg.port_start,
      cfg.port_end);
  printf("%-8s %-8s %-12s %s\n", "PORT", "ETAT", "SERVICE", "VERSION");
  int open_count = 0;
  int filtered_count = 0;
  for (int p = cfg.port_start; p <= cfg.port_end; p++) {
    port_state st;
    int fd = tcp_connect(&addr, len, p, cfg.timeout_ms, &st);
    if (st == PORT_OPEN) {
      char banner[128];
      char name[32];
      char version[128];
      int is_http;
      grab_banner(fd, cfg.timeout_ms, banner, sizeof banner, &is_http);
      close(fd);
      identify_service(p, banner, is_http, name, sizeof name, version,
          sizeof version);
      printf("%-8d %-8s %-12s %s\n", p, state_str(st), name, version);
      open_count++;
    } else if (st == PORT_FILTERED) {
      filtered_count++;
    }
  }
  printf("\n%d port(s) ouvert(s), %d filtre(s)\n", open_count, filtered_count);
  return 0;
}
