#ifndef GUARD_H
#define GUARD_H

#include <sys/socket.h>

/*
 * Module guard : limite les cibles du scanner aux reseaux locaux et
 * prives, pour eviter de scanner par erreur une machine sur Internet.
 */

/*
 * is_private_target : indique si une adresse appartient a une plage non
 * routee sur Internet.
 *
 * Plages acceptees :
 *   IPv4 : 127.0.0.0/8 (loopback), 10.0.0.0/8, 172.16.0.0/12,
 *          192.168.0.0/16 (prives, RFC 1918), 169.254.0.0/16 (lien local)
 *   IPv6 : ::1 (loopback), fc00::/7 (ULA), fe80::/10 (lien local),
 *          et les adresses IPv4 mappees (::ffff:a.b.c.d) selon les
 *          regles IPv4.
 *
 * Parametres :
 *   addr : adresse resolue par resolve_host, ne doit pas etre NULL.
 *
 * Retour : 1 si l'adresse est dans une de ces plages, 0 sinon (y compris
 * pour une famille d'adresse inconnue).
 */
int is_private_target(const struct sockaddr_storage *addr);

/*
 * addr_to_str : ecrit l'adresse IP de addr sous forme texte.
 *
 * Parametres :
 *   addr : adresse IPv4 ou IPv6.
 *   buf  : tampon de destination.
 *   size : taille de buf ; INET6_ADDRSTRLEN (46) suffit dans tous les cas.
 *
 * Retour : buf, qui contient l'adresse, ou "?" si la conversion echoue.
 */
const char *addr_to_str(const struct sockaddr_storage *addr, char *buf,
    size_t size);

#endif
