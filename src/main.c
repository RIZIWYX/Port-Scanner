#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "cli.h"
#include "net.h"
#include "guard.h"
#include "banner.h"
#include "service.h"

/*
 * elapsed_s : secondes ecoulees depuis start (horloge monotone).
 */
static double elapsed_s(const struct timespec *start) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double) (now.tv_sec - start->tv_sec)
      + (double) (now.tv_nsec - start->tv_nsec) / 1e9;
}

/*
 * print_open_port : se reconnecte a un port ouvert, recupere sa banniere,
 * identifie le service et affiche la ligne de resultat.
 *
 * Parametres :
 *   addr, len  : adresse de la cible.
 *   port       : port detecte ouvert par scan_range.
 *   timeout_ms : attente maximale pour la connexion et pour la banniere.
 *
 * Si le port s'est ferme entre-temps, il est affiche sans banniere.
 */
static void print_open_port(const struct sockaddr_storage *addr,
    socklen_t len, int port, int timeout_ms) {
  char banner[128] = "";
  char name[32];
  char version[128];
  int is_http = 0;
  port_state st;
  int fd = tcp_connect(addr, len, port, timeout_ms, &st);
  if (fd >= 0) {
    grab_banner(fd, timeout_ms, banner, sizeof banner, &is_http);
    close(fd);
  }
  identify_service(port, banner, is_http, name, sizeof name, version,
      sizeof version);
  printf("%-8d %-8s %-12s %s\n", port, state_str(PORT_OPEN), name,
      version);
}

/*
 * main : scanne une plage de ports TCP d'une cible et affiche les ports
 * ouverts avec le service identifie et sa version, puis un bilan.
 *
 * Deroulement :
 *   1. lecture des arguments, resolution de l'hote ;
 *   2. garde-fou : une cible hors des plages locales et privees est
 *      refusee, sauf avec --allow-public ;
 *   3. scan parallele de la plage (scan_range) ;
 *   4. pour chaque port ouvert : banniere et identification du service.
 *
 * Retour : 0 si le scan a eu lieu, 1 si les arguments sont invalides,
 * si l'hote ne peut pas etre resolu ou si le scan echoue, 2 si la cible
 * est refusee par le garde-fou.
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
  int count = cfg.port_end - cfg.port_start + 1;
  port_state *states = malloc((size_t) count * sizeof *states);
  if (states == NULL) {
    perror("malloc");
    return 1;
  }
  printf("Scan de %s (%s), ports %d-%d\n\n", cfg.host, ip, cfg.port_start,
      cfg.port_end);
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  if (scan_range(&addr, len, cfg.port_start, cfg.port_end, cfg.timeout_ms,
      cfg.parallel, states) < 0) {
    fprintf(stderr, "Echec du scan\n");
    free(states);
    return 1;
  }
  printf("%-8s %-8s %-12s %s\n", "PORT", "ETAT", "SERVICE", "VERSION");
  int open_count = 0;
  int filtered_count = 0;
  for (int i = 0; i < count; i++) {
    if (states[i] == PORT_OPEN) {
      print_open_port(&addr, len, cfg.port_start + i, cfg.timeout_ms);
      open_count++;
    } else if (states[i] == PORT_FILTERED) {
      filtered_count++;
    }
  }
  printf("\n%d port(s) ouvert(s), %d filtre(s), %d ferme(s) sur %d, "
      "en %.2f s\n", open_count, filtered_count,
      count - open_count - filtered_count, count, elapsed_s(&start));
  free(states);
  return 0;
}
