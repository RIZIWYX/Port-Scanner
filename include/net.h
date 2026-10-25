#ifndef NET_H
#define NET_H

#include <sys/socket.h>

/*
 * Module net : resolution d'hote et test d'un port TCP.
 */

/*
 * port_state : etat d'un port apres une tentative de connexion TCP.
 *   PORT_OPEN     : la connexion a abouti (SYN-ACK recu), un service
 *                   ecoute sur ce port.
 *   PORT_CLOSED   : la cible a refuse la connexion (RST recu), aucun
 *                   service n'ecoute.
 *   PORT_FILTERED : aucune reponse dans le delai, ou cible signalee
 *                   injoignable : un pare-feu filtre probablement.
 *   PORT_ERROR    : erreur locale (creation du socket, appel systeme...),
 *                   l'etat du port est inconnu.
 */
typedef enum {
  PORT_OPEN, PORT_CLOSED, PORT_FILTERED, PORT_ERROR
} port_state;

/*
 * resolve_host : convertit un nom d'hote ou une adresse IP textuelle en
 * adresse binaire utilisable par connect.
 *
 * Parametres :
 *   host : nom (ex. "localhost") ou adresse IPv4/IPv6 (ex. "127.0.0.1",
 *          "::1").
 *   addr : zone ou ecrire l'adresse, ne doit pas etre NULL.
 *   len  : recoit la taille utile de l'adresse ecrite dans *addr.
 *
 * Retour :
 *   0 en cas de succes : *addr contient la premiere adresse trouvee, avec
 *   un port a 0 (il est fixe ensuite par scan_port).
 *   -1 si la resolution echoue : un message est ecrit sur stderr, *addr
 *   et *len ne sont pas modifies.
 */
int resolve_host(const char *host, struct sockaddr_storage *addr,
    socklen_t *len);

/*
 * scan_port : tente une connexion TCP sur un port et en deduit son etat.
 *
 * Parametres :
 *   addr       : adresse de la cible obtenue par resolve_host. Elle n'est
 *                pas modifiee : le port est place dans une copie.
 *   len        : taille de l'adresse, telle que renvoyee par resolve_host.
 *   port       : port a tester, dans [1, 65535].
 *   timeout_ms : duree maximale d'attente de la reponse, en millisecondes.
 *
 * Retour : l'etat du port (voir port_state).
 *
 * Comportement : la connexion est non bloquante, l'appel dure donc au plus
 * environ timeout_ms. Le socket est toujours ferme avant le retour, meme
 * en cas d'erreur. Si la connexion aboutit, elle est fermee aussitot.
 */
port_state scan_port(const struct sockaddr_storage *addr, socklen_t len,
    int port, int timeout_ms);

/*
 * state_str : renvoie le libelle francais d'un etat de port.
 *
 * Retour : "ouvert", "ferme", "filtre" ou "erreur". La chaine est
 * constante, elle ne doit ni etre modifiee ni etre liberee.
 */
const char *state_str(port_state s);

#endif
