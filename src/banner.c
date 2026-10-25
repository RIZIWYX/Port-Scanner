#include <string.h>
#include <strings.h>
#include <poll.h>
#include <sys/socket.h>
#include "banner.h"

#define RAW_SIZE 1024

/*
 * recv_timeout : attend au plus timeout_ms que des donnees arrivent sur
 * fd, puis les lit.
 *
 * Parametres :
 *   fd         : socket connecte.
 *   timeout_ms : attente maximale, en millisecondes.
 *   buf, size  : tampon de reception ; au plus size - 1 octets sont lus
 *                et buf est termine par '\0'.
 *
 * Les octets nuls recus sont remplaces par '.', pour que buf reste une
 * chaine C exploitable.
 *
 * Retour : le nombre d'octets lus, 0 si rien n'est arrive dans le delai,
 * si le service a ferme la connexion ou en cas d'erreur.
 */
static size_t recv_timeout(int fd, int timeout_ms, char *buf, size_t size) {
  struct pollfd pfd = { .fd = fd, .events = POLLIN };
  buf[0] = '\0';
  if (poll(&pfd, 1, timeout_ms) <= 0) {
    return 0;
  }
  ssize_t n = recv(fd, buf, size - 1, 0);
  if (n <= 0) {
    return 0;
  }
  for (ssize_t i = 0; i < n; i++) {
    if (buf[i] == '\0') {
      buf[i] = '.';                       /* service binaire */
    }
  }
  buf[n] = '\0';
  return (size_t) n;
}

/*
 * copy_line : copie dans dst la ligne qui commence a src, sans le saut de
 * ligne final, en remplacant les caracteres non imprimables par '.'.
 *
 * Parametres :
 *   src       : debut de la ligne, terminee par '\r', '\n' ou '\0'.
 *   dst, size : tampon de destination, tronque si la ligne est trop longue
 *               et toujours termine par '\0'.
 *
 * Retour : la longueur de la chaine ecrite dans dst.
 */
static size_t copy_line(const char *src, char *dst, size_t size) {
  size_t n = 0;
  while (src[n] != '\0' && src[n] != '\r' && src[n] != '\n'
      && n < size - 1) {
    unsigned char c = (unsigned char) src[n];
    dst[n] = (c >= 32 && c < 127) ? (char) c : '.';
    n++;
  }
  dst[n] = '\0';
  return n;
}

/*
 * http_server : cherche l'en-tete "Server:" dans une reponse HTTP.
 *
 * Parametres :
 *   resp : reponse HTTP complete, terminee par '\0'.
 *
 * Retour : un pointeur vers le debut de la valeur de l'en-tete (espaces
 * initiaux sautes), ou NULL s'il est absent.
 */
static const char *http_server(const char *resp) {
  for (const char *p = resp; p != NULL; p = strchr(p, '\n')) {
    if (*p == '\n') {
      p++;
    }
    if (strncasecmp(p, "Server:", 7) == 0) {
      p += 7;
      while (*p == ' ' || *p == '\t') {
        p++;
      }
      return p;
    }
  }
  return NULL;
}

size_t grab_banner(int fd, int timeout_ms, char *buf, size_t size) {
  char raw[RAW_SIZE];
  buf[0] = '\0';
  /* 1. Le service parle-t-il en premier ? */
  if (recv_timeout(fd, timeout_ms, raw, sizeof raw) == 0) {
    /* 2. Sinon, sonde HTTP */
    static const char probe[] = "HEAD / HTTP/1.0\r\n\r\n";
    if (send(fd, probe, sizeof probe - 1, MSG_NOSIGNAL) < 0
        || recv_timeout(fd, timeout_ms, raw, sizeof raw) == 0) {
      return 0;
    }
  }
  /* 3. Extraction de la ligne utile */
  if (strncmp(raw, "HTTP/", 5) == 0) {
    const char *server = http_server(raw);
    if (server != NULL) {
      return copy_line(server, buf, size);
    }
  }
  return copy_line(raw, buf, size);
}
