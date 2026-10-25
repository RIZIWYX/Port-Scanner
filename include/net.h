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
 * tcp_connect : ouvre une connexion TCP non bloquante vers un port, en
 * attendant au plus timeout_ms.
 *
 * Parametres :
 *   addr, len  : adresse de la cible, obtenue par resolve_host (non
 *                modifiee : le port est place dans une copie).
 *   port       : port a contacter, dans [1, 65535].
 *   timeout_ms : duree maximale d'attente, en millisecondes.
 *   state      : recoit l'etat du port (voir port_state), ne doit pas
 *                etre NULL.
 *
 * Retour :
 *   le descripteur du socket connecte si *state vaut PORT_OPEN ; il reste
 *   en mode non bloquant et l'appelant doit le fermer avec close.
 *   -1 dans tous les autres cas ; aucun socket ne reste alors ouvert.
 */
int tcp_connect(const struct sockaddr_storage *addr, socklen_t len,
    int port, int timeout_ms, port_state *state);

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
 * scan_range : scanne une plage de ports en menant plusieurs connexions
 * en parallele.
 *
 * Fonctionnement : jusqu'a max_parallel connexions non bloquantes sont
 * lancees en meme temps et surveillees par un seul appel a poll. Des
 * qu'une connexion se termine (reponse recue ou delai depasse), sa place
 * est donnee au port suivant. La duree totale est donc d'environ
 * (nombre de ports / max_parallel) * timeout_ms dans le pire cas, au lieu
 * de nombre de ports * timeout_ms en sequentiel.
 *
 * Parametres :
 *   addr, len    : adresse de la cible, obtenue par resolve_host.
 *   port_start   : premier port, dans [1, 65535].
 *   port_end     : dernier port, dans [port_start, 65535].
 *   timeout_ms   : attente maximale par port, en millisecondes.
 *   max_parallel : nombre maximal de connexions simultanees, au moins 1.
 *                  Il doit rester sous la limite de descripteurs du
 *                  processus (ulimit -n) ; si elle est atteinte, le scan
 *                  attend qu'une place se libere au lieu d'echouer.
 *   states       : tableau d'au moins port_end - port_start + 1 cases ;
 *                  states[p - port_start] recoit l'etat du port p.
 *
 * Retour : 0 si tous les ports ont ete scannes, -1 en cas d'erreur
 * d'allocation ou de poll (le contenu de states est alors incomplet).
 * Tous les sockets sont fermes avant le retour.
 */
int scan_range(const struct sockaddr_storage *addr, socklen_t len,
    int port_start, int port_end, int timeout_ms, int max_parallel,
    port_state *states);

/*
 * state_str : renvoie le libelle francais d'un etat de port.
 *
 * Retour : "ouvert", "ferme", "filtre" ou "erreur". La chaine est
 * constante, elle ne doit ni etre modifiee ni etre liberee.
 */
const char *state_str(port_state s);

#endif
