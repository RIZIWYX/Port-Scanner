#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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

/*
 * error_to_state : traduit le code d'erreur d'une tentative de connexion
 * (errno ou SO_ERROR) en etat de port.
 * Retour : PORT_OPEN pour 0, PORT_CLOSED pour un refus, PORT_FILTERED si
 * la cible est injoignable ou muette, PORT_ERROR sinon.
 */
static port_state error_to_state(int err) {
  switch (err) {
    case 0:
      return PORT_OPEN;
    case ECONNREFUSED:
      return PORT_CLOSED;
    case EHOSTUNREACH:
    case ENETUNREACH:
    case ETIMEDOUT:
      return PORT_FILTERED;
    default:
      return PORT_ERROR;
  }
}

/*
 * start_connect : cree un socket non bloquant et lance la connexion.
 *
 * Parametres :
 *   base, len : adresse de la cible (copiee, non modifiee).
 *   port      : port a contacter.
 *   fd        : recoit le descripteur du socket, ou -1 s'il a ete ferme.
 *   err       : recoit errno en cas d'echec de socket() ou fcntl().
 *
 * Retour :
 *   1 si la connexion est en cours : *fd est ouvert et deviendra
 *     inscriptible quand la cible aura repondu ;
 *   0 si le resultat est deja connu : il est alors renvoye dans *state et
 *     *fd n'est ouvert que si *state vaut PORT_OPEN ;
 *   -1 si le socket n'a pas pu etre cree ou configure (*fd vaut -1).
 */
static int start_connect(const struct sockaddr_storage *base, socklen_t len,
    int port, int *fd, port_state *state, int *err) {
  struct sockaddr_storage addr = *base;
  set_port(&addr, port);
  *fd = socket(addr.ss_family, SOCK_STREAM, 0);
  if (*fd < 0) {
    *err = errno;
    return -1;
  }
  int flags = fcntl(*fd, F_GETFL, 0);
  if (flags < 0 || fcntl(*fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    *err = errno;
    close(*fd);
    *fd = -1;
    return -1;
  }
  if (connect(*fd, (struct sockaddr *) &addr, len) == 0) {
    *state = PORT_OPEN;                   /* connexion locale instantanee */
    return 0;
  }
  if (errno == EINPROGRESS) {
    return 1;
  }
  *state = error_to_state(errno);
  close(*fd);
  *fd = -1;
  return 0;
}

/*
 * finish_connect : lit le resultat d'une connexion non bloquante terminee
 * (socket devenu inscriptible ou en erreur selon poll).
 * Retour : l'etat du port deduit de SO_ERROR.
 */
static port_state finish_connect(int fd) {
  int err = 0;
  socklen_t elen = sizeof err;
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0) {
    return PORT_ERROR;
  }
  return error_to_state(err);
}

int tcp_connect(const struct sockaddr_storage *base, socklen_t len,
    int port, int timeout_ms, port_state *state) {
  int fd;
  int err;
  *state = PORT_ERROR;
  int rc = start_connect(base, len, port, &fd, state, &err);
  if (rc == 1) {
    /* Attente bornee de la reponse */
    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    int prc = poll(&pfd, 1, timeout_ms);
    if (prc == 0) {
      *state = PORT_FILTERED;             /* aucune reponse dans le delai */
    } else if (prc < 0) {
      *state = PORT_ERROR;
    } else {
      *state = finish_connect(fd);
    }
  }
  if (*state != PORT_OPEN) {
    if (fd >= 0) {
      close(fd);
    }
    return -1;
  }
  return fd;
}

/*
 * now_ms : temps ecoule depuis un instant de reference fixe, en
 * millisecondes (horloge monotone, insensible aux changements d'heure).
 */
static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * pending : connexion en cours dans scan_range.
 *   fd       : socket non bloquant en attente de reponse.
 *   port     : port contacte.
 *   deadline : instant (now_ms) au-dela duquel le port est declare filtre.
 */
struct pending {
  int fd;
  int port;
  long long deadline;
};

int scan_range(const struct sockaddr_storage *addr, socklen_t len,
    int port_start, int port_end, int timeout_ms, int max_parallel,
    port_state *states) {
  struct pending *slots = malloc((size_t) max_parallel * sizeof *slots);
  struct pollfd *pfds = malloc((size_t) max_parallel * sizeof *pfds);
  if (slots == NULL || pfds == NULL) {
    free(slots);
    free(pfds);
    return -1;
  }
  int active = 0;
  int next = port_start;
  while (next <= port_end || active > 0) {
    /* 1. Remplir les places libres avec de nouvelles connexions */
    while (active < max_parallel && next <= port_end) {
      int fd;
      int err;
      port_state st = PORT_ERROR;
      int rc = start_connect(addr, len, next, &fd, &st, &err);
      if (rc < 0 && (err == EMFILE || err == ENFILE) && active > 0) {
        break;                  /* plus de descripteurs : on reessaiera */
      }
      if (rc == 1) {
        slots[active].fd = fd;
        slots[active].port = next;
        slots[active].deadline = now_ms() + timeout_ms;
        active++;
      } else {
        states[next - port_start] = st;
        if (fd >= 0) {
          close(fd);
        }
      }
      next++;
    }
    if (active == 0) {
      continue;
    }
    /* 2. Attendre jusqu'a la plus proche echeance */
    long long now = now_ms();
    long long first = slots[0].deadline;
    for (int i = 0; i < active; i++) {
      pfds[i].fd = slots[i].fd;
      pfds[i].events = POLLOUT;
      pfds[i].revents = 0;
      if (slots[i].deadline < first) {
        first = slots[i].deadline;
      }
    }
    int wait = first > now ? (int) (first - now) : 0;
    if (poll(pfds, (nfds_t) active, wait) < 0 && errno != EINTR) {
      for (int i = 0; i < active; i++) {
        close(slots[i].fd);
      }
      free(slots);
      free(pfds);
      return -1;
    }
    /* 3. Traiter les connexions terminees ou expirees. Parcours a
     *    l'envers : une case liberee recoit la derniere, deja traitee. */
    now = now_ms();
    for (int i = active - 1; i >= 0; i--) {
      port_state st;
      if (pfds[i].revents != 0) {
        st = finish_connect(slots[i].fd);
      } else if (now >= slots[i].deadline) {
        st = PORT_FILTERED;     /* aucune reponse dans le delai */
      } else {
        continue;
      }
      states[slots[i].port - port_start] = st;
      close(slots[i].fd);
      slots[i] = slots[active - 1];
      active--;
    }
  }
  free(slots);
  free(pfds);
  return 0;
}

port_state scan_port(const struct sockaddr_storage *base, socklen_t len,
    int port, int timeout_ms) {
  port_state st;
  int fd = tcp_connect(base, len, port, timeout_ms, &st);
  if (fd >= 0) {
    close(fd);
  }
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
