# Historique du développement

Le projet a été construit en huit étapes, chacune testée et committée avant la suivante.

| Étape | Contenu | Résultat |
|---|---|---|
| 1 | structure, Makefile, ligne de commande | arguments validés |
| 2 | scan TCP connect | ports ouverts détectés sur localhost |
| 3 | connexion non bloquante avec délai | 6 ports filtrés en 1,8 s au lieu de ~12 min |
| — | réorganisation en modules, `obj/`, suivi des headers, spécifications | architecture actuelle |
| 4 | garde-fou | cibles publiques refusées |
| 5 | récupération de bannière | SSH, SMTP, HTTP... lisibles |
| 6 | identification du service | nom et version affichés |
| 7 | scan parallèle avec `poll` | 20 ports filtrés : 6,01 s → 0,30 s |
| 8 | tests automatiques, comparaison nmap, documentation | 26 tests, accord total avec nmap sur les états |

## Étape 1 : structure et ligne de commande

- Arborescence `src/`, `include/`, `tests/`, compilation stricte `-Wall -Wextra -Werror -std=c11`.
- `parse_args` avec `strtol` plutôt que `atoi`, pour rejeter `"80abc"` ou une chaîne vide.

## Étape 2 : scan TCP connect

- `getaddrinfo` pour la résolution, `sockaddr_storage` pour supporter IPv4 et IPv6 sans code supplémentaire.
- Un `connect()` par port ; l'état est déduit de son retour et de `errno`.
- **Limite constatée** : `connect()` bloquant, environ deux minutes par port filtré.

## Étape 3 : connexion non bloquante

- `O_NONBLOCK`, `poll(POLLOUT)` avec délai, résultat lu par `getsockopt(SO_ERROR)`.
- Validé sur une adresse qui ne répond pas : 6 ports filtrés en 1,8 s avec un délai de 300 ms.

## Réorganisation

- **Problème** : un seul header `scanner.h` pour tout, un fichier `scanner.c` qui ne faisait que lire les arguments, des `.o` mélangés aux sources.
- **Bug trouvé** : le Makefile ne suivait pas les headers ; modifier un `.h` ne recompilait rien.
- **Correction** : un module par responsabilité (`cli`, `net`), objets dans `obj/`, dépendances générées par `-MMD -MP`, spécification au-dessus de chaque fonction.

## Étape 4 : garde-fou

- Vérification sur l'IP résolue, plages IPv4 et IPv6 non routées, option `--allow-public`, code de sortie 2.
- **Cas piège traité** : `::ffff:8.8.8.8`, une IPv4 publique écrite en IPv6, qui aurait contourné un contrôle IPv6 naïf.

## Étape 5 : bannière

- `tcp_connect` extrait de `scan_port` pour garder la connexion ouverte et parler au service.
- Écoute d'abord, sonde `HEAD / HTTP/1.0` ensuite, extraction de l'en-tête `Server:`.
- **Bug trouvé** : une bannière commençant par un octet nul était vue comme vide, car les fonctions de chaînes C s'arrêtent au premier `'\0'`. Correction : octets nuls remplacés dès la réception.
- `MSG_NOSIGNAL` pour qu'un service qui ferme la connexion ne tue pas le scanner.

## Étape 6 : identification du service

- Motifs de bannière pour SSH, HTTP, FTP, SMTP, POP3, IMAP, VNC ; repli sur `getservbyport` avec un `?`.
- Ajout d'un indicateur `is_http` à `grab_banner` : après extraction de `Server:`, l'origine HTTP était perdue.

## Étape 7 : scan parallèle

- `scan_range` : jusqu'à `-c` connexions en cours, un seul `poll`, échéance par connexion sur horloge monotone.
- Code commun factorisé (`start_connect`, `finish_connect`, `error_to_state`) : `tcp_connect` et `scan_range` partagent la même logique de connexion.
- Gestion de `EMFILE` : attente d'une place libre au lieu d'un échec, validée avec `ulimit -n 64`.
- Scan en deux passes : états de tous les ports, puis bannières des ports ouverts.

## Étape 8 : validation et documentation

- 26 tests automatiques sur des faux services, indépendants de la machine.
- valgrind : aucune erreur, aucune fuite, aucun descripteur oublié.
- Comparaison avec nmap : mêmes états sur tous les ports, même service sur 7 ports sur 8.
- README et documentation technique.

## Pistes pour la suite

- Lire les bannières dans la même boucle `poll` que le scan, pour paralléliser aussi cette passe.
- Liste de ports (`-p 22,80,443`) et plages d'adresses (CIDR).
- Sortie JSON ou CSV, pour exploiter les résultats dans d'autres outils.
- Nouvelles sondes (Redis, MySQL) et support de TLS pour identifier HTTPS.
- Scan UDP.
