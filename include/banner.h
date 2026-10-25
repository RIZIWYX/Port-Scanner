#ifndef BANNER_H
#define BANNER_H

#include <stddef.h>

/*
 * Module banner : recuperation de la banniere d'un service, c'est-a-dire
 * le texte qu'il envoie et qui permet de l'identifier.
 */

/*
 * grab_banner : lit la banniere du service joint par un socket connecte.
 *
 * Fonctionnement :
 *   1. Attend jusqu'a timeout_ms que le service parle en premier : c'est
 *      le cas de SSH, FTP, SMTP, POP3...
 *   2. S'il reste muet, lui envoie une requete HTTP minimale
 *      ("HEAD / HTTP/1.0") et attend de nouveau jusqu'a timeout_ms.
 *   3. Si la reponse est du HTTP, garde la valeur de l'en-tete "Server"
 *      (ou la ligne de statut s'il n'y en a pas) ; sinon garde la
 *      premiere ligne recue.
 *   Les caracteres non imprimables sont remplaces par '.'.
 *
 * Parametres :
 *   fd         : socket connecte (par exemple renvoye par tcp_connect),
 *                bloquant ou non. Il n'est pas ferme par la fonction.
 *   timeout_ms : attente maximale pour chacune des deux phases ; l'appel
 *                dure donc au plus environ 2 * timeout_ms.
 *   buf        : tampon recevant la banniere, terminee par '\0'.
 *   size       : taille de buf, au moins 2.
 *
 * Retour : la longueur de la banniere ecrite dans buf, ou 0 si le service
 * n'a rien envoye (buf contient alors la chaine vide).
 */
size_t grab_banner(int fd, int timeout_ms, char *buf, size_t size);

#endif
