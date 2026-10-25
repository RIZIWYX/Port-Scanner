#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "service.h"

/*
 * contains_icase : indique si une chaine en contient une autre, sans tenir
 * compte de la casse.
 * Retour : 1 si needle apparait dans haystack, 0 sinon.
 */
static int contains_icase(const char *haystack, const char *needle) {
  size_t n = strlen(needle);
  for (const char *p = haystack; *p != '\0'; p++) {
    if (strncasecmp(p, needle, n) == 0) {
      return 1;
    }
  }
  return 0;
}

/*
 * service_from_banner : reconnait un protocole d'apres sa banniere.
 *
 * Parametres :
 *   banner  : banniere nettoyee.
 *   is_http : 1 si la banniere provient d'une reponse HTTP.
 *
 * Retour : le nom du protocole ("ssh", "http"...) ou NULL s'il n'est pas
 * reconnu. La chaine est constante.
 */
static const char *service_from_banner(const char *banner, int is_http) {
  if (is_http) {
    return "http";
  }
  if (strncmp(banner, "SSH-", 4) == 0) {
    return "ssh";
  }
  if (strncmp(banner, "220", 3) == 0) {
    if (contains_icase(banner, "SMTP")) {
      return "smtp";
    }
    if (contains_icase(banner, "FTP")) {
      return "ftp";
    }
  }
  if (strncmp(banner, "+OK", 3) == 0) {
    return "pop3";
  }
  if (strncmp(banner, "* OK", 4) == 0) {
    return "imap";
  }
  if (strncmp(banner, "RFB ", 4) == 0) {
    return "vnc";
  }
  return NULL;
}

/*
 * ssh_version : extrait la partie logiciel d'une banniere SSH.
 * "SSH-2.0-OpenSSH_9.6p1 Ubuntu" donne "OpenSSH 9.6p1 Ubuntu" : on saute
 * le prefixe "SSH-<protocole>-" et on remplace le premier '_' par un
 * espace.
 *
 * Parametres :
 *   banner    : banniere commencant par "SSH-".
 *   out, size : tampon de destination.
 */
static void ssh_version(const char *banner, char *out, size_t size) {
  const char *soft = strchr(banner + 4, '-');
  snprintf(out, size, "%s", soft != NULL ? soft + 1 : banner);
  char *underscore = strchr(out, '_');
  if (underscore != NULL) {
    *underscore = ' ';
  }
}

void identify_service(int port, const char *banner, int is_http,
    char *name, size_t name_size, char *version, size_t ver_size) {
  const char *known = service_from_banner(banner, is_http);
  if (known != NULL) {
    snprintf(name, name_size, "%s", known);
  } else {
    struct servent *se = getservbyport(htons((unsigned short) port), "tcp");
    if (se != NULL) {
      snprintf(name, name_size, "%s?", se->s_name);
    } else {
      snprintf(name, name_size, "inconnu");
    }
  }
  if (known != NULL && strcmp(known, "ssh") == 0) {
    ssh_version(banner, version, ver_size);
  } else {
    snprintf(version, ver_size, "%s", banner);
  }
}
