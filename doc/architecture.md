# Architecture

## Vue d'ensemble

Le programme est découpé en six modules. Chacun a une responsabilité unique et une interface dans `include/`, son implémentation dans `src/`.

```
                        ┌──────────┐
                        │  main.c  │  orchestration et affichage
                        └────┬─────┘
       ┌──────────┬──────────┼──────────┬───────────┐
       ▼          ▼          ▼          ▼           ▼
   ┌───────┐  ┌───────┐  ┌───────┐  ┌────────┐  ┌─────────┐
   │  cli  │  │  net  │  │ guard │  │ banner │  │ service │
   └───────┘  └───────┘  └───────┘  └────────┘  └─────────┘
   arguments  sockets,   plages     lecture     nom et
              scan       autorisées bannière    version
```

| Module | Fichiers | Responsabilité | Dépend de |
|---|---|---|---|
| `cli` | `cli.h`, `cli.c` | lire et valider la ligne de commande | libc |
| `net` | `net.h`, `net.c` | résolution d'hôte, connexion non bloquante, scan parallèle | API sockets |
| `guard` | `guard.h`, `guard.c` | n'autoriser que les adresses locales et privées | API sockets |
| `banner` | `banner.h`, `banner.c` | récupérer et nettoyer la bannière d'un service | API sockets |
| `service` | `service.h`, `service.c` | déduire le service et sa version | `getservbyport` |
| `main` | `main.c` | enchaîner les étapes, afficher le résultat | tous les modules |

Aucun module ne dépend d'un autre module, à part `main`. Ils peuvent donc être testés, modifiés ou réutilisés séparément : `banner` fonctionne sur n'importe quel socket connecté, quel que soit le moyen par lequel il a été ouvert.

## Flux d'exécution

```
./scanner 127.0.0.1 1 1024 -t 300
        │
        ▼
 1. parse_args ─────────────► scan_config { host, ports, timeout, parallel, allow_public }
        │                        échec → code 1
        ▼
 2. resolve_host ───────────► sockaddr_storage (IPv4 ou IPv6)
        │                        échec → code 1
        ▼
 3. is_private_target ──────► adresse publique sans --allow-public → code 2
        │
        ▼
 4. scan_range ─────────────► tableau states[] : un état par port
        │                        (connexions en parallèle, un seul poll)
        ▼
 5. pour chaque port ouvert :
        tcp_connect ──► grab_banner ──► identify_service ──► ligne affichée
        │
        ▼
 6. bilan : ouverts, filtrés, fermés, durée ──► code 0
```

Le scan se fait en **deux passes** : la passe 4 détermine l'état de tous les ports le plus vite possible, la passe 5 ne s'occupe que des ports ouverts, généralement peu nombreux. Séparer les deux garde `scan_range` simple (il n'a qu'un rôle) et évite de garder des centaines de sockets ouverts en attendant des bannières.

## Données principales

| Structure | Où | Rôle |
|---|---|---|
| `scan_config` | `cli.h` | paramètres du scan, remplis par `parse_args` |
| `struct sockaddr_storage` | standard POSIX | adresse de la cible, assez grande pour IPv4 et IPv6 |
| `port_state` | `net.h` | `PORT_OPEN`, `PORT_CLOSED`, `PORT_FILTERED`, `PORT_ERROR` |
| `port_state states[]` | `main.c` | résultat du scan, indexé par `port - port_start` |
| `struct pending` | `net.c` (interne) | connexion en cours dans `scan_range` : `fd`, `port`, échéance |

## Arborescence

```
Port-Scanner/
├── Makefile
├── README.md             présentation et utilisation
├── LICENSE               MIT
├── doc/                  cette documentation
├── include/              interfaces, chaque fonction y est spécifiée
├── src/                  implémentations
├── tests/
│   ├── run_tests.sh      tests automatiques
│   └── fake_services.py  faux services pour les tests
├── obj/                  généré : fichiers objets et dépendances (ignoré par Git)
└── scanner               généré : exécutable (ignoré par Git)
```

## Compilation

### Options

```makefile
CFLAGS   = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
DEPFLAGS = -MMD -MP
```

| Option | Rôle |
|---|---|
| `-Wall -Wextra` | active la plupart des avertissements |
| `-Werror` | tout avertissement devient une erreur : le code doit être propre pour compiler |
| `-std=c11` | C11 strict, sans extensions GNU |
| `-D_POSIX_C_SOURCE=200809L` | rend visibles les fonctions POSIX (`getaddrinfo`, `poll`, `clock_gettime`...) que `-std=c11` masque |
| `-Iinclude` | répertoire des headers |
| `-MMD -MP` | génère pour chaque `.o` un fichier `.d` listant les headers inclus |

### Cibles

| Commande | Effet |
|---|---|
| `make` | compile `scanner` (seuls les fichiers modifiés sont recompilés) |
| `make test` | compile puis lance `tests/run_tests.sh` |
| `make clean` | supprime `obj/` et `scanner` |
| `make re` | `clean` puis compilation complète |

### Suivi des dépendances

Une règle `obj/%.o: src/%.c` ne connaît que le fichier source. Sans précaution, modifier `net.h` ne recompilerait pas `main.c`, qui l'inclut, et l'exécutable deviendrait incohérent. `-MMD` fait écrire par gcc un fichier `obj/main.d` :

```makefile
obj/main.o: src/main.c include/cli.h include/net.h include/guard.h ...
```

Le Makefile le relit avec `-include $(DEPS)`. `-MP` ajoute une règle vide par header, pour que la suppression d'un header ne casse pas la compilation.

Le prérequis `| $(OBJ_DIR)` est un prérequis *d'ordre seulement* : le dossier `obj/` est créé s'il manque, mais sa date de modification (qui change à chaque nouveau fichier) ne force pas de recompilation.

## Conventions de code

- **Un module = un `.c` + un `.h`** du même nom, avec une garde d'inclusion `NOM_H`.
- **Spécification au-dessus de chaque fonction** : dans le `.h` pour les fonctions publiques, dans le `.c` pour les fonctions `static`. Elle décrit le rôle, chaque paramètre et ses contraintes, la valeur de retour dans tous les cas et les effets de bord.
- **Fonctions internes `static`** : invisibles hors de leur fichier.
- **Erreurs** : les fonctions renvoient `0` / `-1` ou un état ; seuls `cli` et `net` écrivent sur `stderr`. `main` choisit le code de sortie.
- **Ressources** : tout socket ouvert est fermé dans la même fonction ou rendu explicitement à l'appelant (`tcp_connect`), ce qui est documenté.
- **Messages sans accents** dans le code, pour éviter les problèmes d'encodage dans les terminaux.
