#ifndef CLI_H
#define CLI_H

/*
 * Module cli : lecture et validation de la ligne de commande.
 */

/*
 * scan_config : parametres d'un scan.
 *   host       : nom d'hote ou adresse IP (IPv4 ou IPv6) de la cible.
 *                Pointe vers argv, n'est pas a liberer.
 *   port_start : premier port scanne, dans [1, 65535].
 *   port_end   : dernier port scanne, dans [port_start, 65535].
 *   timeout_ms : delai maximal d'attente par port, en millisecondes,
 *                dans [10, 10000]. Vaut 500 par defaut.
 *   parallel   : nombre maximal de connexions simultanees, dans
 *                [1, 1000]. Vaut 100 par defaut ; 1 donne un scan
 *                sequentiel.
 *   allow_public : 1 si l'option --allow-public est donnee, autorisant
 *                  une cible hors des plages locales et privees ; 0 par
 *                  defaut.
 */
typedef struct {
  const char *host;
  int port_start;
  int port_end;
  int timeout_ms;
  int parallel;
  int allow_public;
} scan_config;

/*
 * parse_args : analyse les arguments du programme et remplit *cfg.
 *
 * Syntaxe attendue :
 *   <hote> <port_debut> <port_fin> [-t timeout_ms] [-c parallele]
 *   [--allow-public]
 * Les options sont facultatives et peuvent etre donnees dans n'importe
 * quel ordre apres les trois arguments obligatoires.
 *
 * Parametres :
 *   argc, argv : arguments recus par main.
 *   cfg        : structure a remplir, ne doit pas etre NULL.
 *
 * Retour :
 *   0 si les arguments sont valides, *cfg est alors entierement rempli.
 *   -1 sinon : nombre d'arguments incorrect, port hors de [1, 65535],
 *   port_debut > port_fin, option inconnue, timeout hors de
 *   [10, 10000] ou parallelisme hors de [1, 1000]. Un message
 *   d'erreur est ecrit sur stderr et le contenu
 *   de *cfg est indetermine.
 *
 * Note : l'hote n'est pas verifie ici, voir resolve_host.
 */
int parse_args(int argc, char **argv, scan_config *cfg);

#endif
