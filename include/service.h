#ifndef SERVICE_H
#define SERVICE_H

#include <stddef.h>

/*
 * Module service : identification du service qui ecoute sur un port, a
 * partir de son numero et de sa banniere.
 */

/*
 * identify_service : determine le nom du service et sa version.
 *
 * Methode :
 *   1. Si la banniere correspond a un protocole connu, le service est
 *      identifie par elle : SSH ("SSH-"), HTTP (reponse HTTP), FTP et SMTP
 *      (code "220" suivi de "FTP" ou "SMTP"), POP3 ("+OK"), IMAP
 *      ("* OK"), VNC ("RFB ").
 *   2. Sinon, le nom est deduit du numero de port grace a la base des
 *      services du systeme (getservbyport, fichier /etc/services), suivi
 *      d'un '?' pour indiquer une simple supposition.
 *   3. A defaut, le nom est "inconnu".
 *
 * Parametres :
 *   port      : numero du port, dans [1, 65535].
 *   banner    : banniere nettoyee renvoyee par grab_banner, eventuellement
 *               vide ; ne doit pas etre NULL.
 *   is_http   : 1 si la banniere provient d'une reponse HTTP, 0 sinon.
 *   name      : recoit le nom du service (ex. "ssh", "http?").
 *   name_size : taille de name.
 *   version   : recoit la version ou, a defaut, la banniere brute ; chaine
 *               vide si la banniere est vide. Pour SSH, la partie logiciel
 *               est extraite ("SSH-2.0-OpenSSH_9.6" donne "OpenSSH 9.6").
 *   ver_size  : taille de version.
 *
 * Les chaines ecrites sont toujours terminees par '\0' et tronquees si
 * necessaire.
 */
void identify_service(int port, const char *banner, int is_http,
    char *name, size_t name_size, char *version, size_t ver_size);

#endif
