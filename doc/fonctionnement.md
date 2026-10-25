# Fonctionnement interne

Ce document explique chaque étape du scan : le principe réseau, l'algorithme, et les choix techniques.

## 1. Rappels réseau

### Adresse et port

Une adresse IP désigne une machine, un port désigne un programme sur cette machine. Un programme serveur « ouvre » un port avec `socket()`, `bind()` et `listen()` ; le noyau lui transmet alors toute connexion arrivant sur ce port. **Un port ouvert est donc un processus en écoute.** On peut le vérifier de l'intérieur avec `sudo ss -ltnp`.

### La poignée de main TCP

```
client                         serveur
  │ ──────── SYN ────────────► │   « je veux me connecter au port 80 »
  │ ◄─────── SYN-ACK ───────── │   « d'accord » (un programme écoute)
  │ ──────── ACK ────────────► │   connexion établie
```

Si aucun programme n'écoute, le serveur répond **RST** (refus). Si un pare-feu jette les paquets, il n'y a **aucune réponse**.

### Les trois états

| Ce que reçoit le client | État | Ce qu'on en sait |
|---|---|---|
| SYN-ACK | ouvert | un service écoute |
| RST | fermé | la machine est joignable, aucun service |
| rien, ou ICMP « injoignable » | filtré | on ne sait rien du port lui-même |

## 2. Résolution de l'hôte

`resolve_host` appelle `getaddrinfo` avec `AF_UNSPEC` (IPv4 ou IPv6) et `SOCK_STREAM` (TCP), copie la première adresse dans une `struct sockaddr_storage` puis libère la liste avec `freeaddrinfo`.

- `getaddrinfo` remplace `gethostbyname`, obsolète et limité à IPv4.
- `sockaddr_storage` peut contenir une `sockaddr_in` (16 octets) comme une `sockaddr_in6` (28 octets) ; le champ `ss_family` dit laquelle. Tout le reste du code est ainsi indépendant de la version d'IP.
- Les erreurs de `getaddrinfo` ne passent pas par `errno` mais par son code de retour, traduit avec `gai_strerror`.

Le port est écrit plus tard, port par port, par `set_port`, avec `htons` : le processeur x86 stocke les entiers en *little-endian* alors que le réseau impose le *big-endian*. Sans conversion, le port 80 (`0x0050`) partirait comme le port 20480 (`0x5000`).

## 3. Connexion non bloquante

Un `connect()` bloquant face à un port filtré attend que le noyau abandonne : Linux retransmet le SYN plusieurs fois, ce qui prend environ deux minutes. Pour imposer notre propre délai :

```
socket() ─► fcntl(O_NONBLOCK) ─► connect()
                                    │
              ┌─────────────────────┼──────────────────────────┐
              ▼                     ▼                          ▼
        renvoie 0            échec immédiat              EINPROGRESS
     (local, instantané)   (ECONNREFUSED...)           connexion en cours
          OUVERT           état selon errno                    │
                                                               ▼
                                                 poll(POLLOUT, timeout_ms)
                                                   │                   │
                                               délai écoulé      socket prêt
                                                FILTRÉ                 │
                                                                       ▼
                                                        getsockopt(SO_ERROR)
                                                    0 → OUVERT
                                                    ECONNREFUSED → FERMÉ
                                                    EHOSTUNREACH, ENETUNREACH,
                                                    ETIMEDOUT → FILTRÉ
```

Points importants :

- **`fcntl(F_GETFL)` avant `F_SETFL`** : on ajoute `O_NONBLOCK` aux drapeaux existants au lieu de les écraser.
- **Socket inscriptible ne veut pas dire connecté** : `poll` signale seulement que la tentative est terminée. Le résultat réel est lu avec `getsockopt(SO_ERROR)`.
- **`errno` lu immédiatement** après l'appel qui a échoué, avant tout autre appel système qui pourrait l'écraser.
- **Traduction centralisée** : `error_to_state` convertit un code (`errno` ou `SO_ERROR`) en état ; le même tableau de correspondance sert partout.

Dans le code, ce schéma est découpé en deux fonctions internes réutilisées par le scan séquentiel et parallèle : `start_connect` (socket, non bloquant, `connect`) et `finish_connect` (lecture de `SO_ERROR`). `tcp_connect` les combine avec un `poll` sur un seul socket.

## 4. Scan parallèle

### Problème

En séquentiel, le pire cas vaut `nombre de ports × timeout` : 1000 ports filtrés à 500 ms, plus de 8 minutes. Pendant presque tout ce temps, le programme ne fait qu'attendre.

### Solution : plusieurs connexions, un seul `poll`

`scan_range` garde jusqu'à `max_parallel` connexions en cours dans un tableau de `struct pending { fd, port, deadline }` :

```
tant qu'il reste des ports à lancer ou des connexions en cours :

  1. REMPLIR   tant qu'il y a une place libre et un port à lancer :
                 start_connect(port)
                 résultat immédiat → noter l'état
                 en cours          → ajouter { fd, port, maintenant + timeout }

  2. ATTENDRE  construire le tableau pollfd de toutes les connexions en cours
               poll(..., échéance la plus proche - maintenant)

  3. TRAITER   pour chaque connexion (parcours à l'envers) :
                 prête    → finish_connect → noter l'état, fermer
                 expirée  → FILTRÉ, fermer
                 sinon    → laisser en place
               une place libérée reçoit la dernière connexion du tableau
```

Exemple avec `max_parallel = 3` sur les ports 1 à 5, où le port 2 est ouvert et les autres filtrés (timeout 300 ms) :

```
t = 0 ms    lance 1, 2, 3            en cours : [1, 2, 3]
t = 1 ms    2 répond SYN-ACK → OUVERT, place libre, lance 4
                                     en cours : [1, 3, 4]
t = 300 ms  1 et 3 expirent → FILTRÉS, lance 5
                                     en cours : [4, 5]
t = 301 ms  4 expire → FILTRÉ        en cours : [5]
t = 600 ms  5 expire → FILTRÉ        terminé
```

Durée totale : 600 ms au lieu de 1200 ms en séquentiel. Avec 100 connexions en parallèle, la différence devient très grande : 20 ports filtrés passent de 6,01 s à 0,30 s.

### Choix techniques

- **`poll` plutôt que des threads** : un seul fil d'exécution, pas de synchronisation, pas de pile par thread. C'est le modèle des serveurs réseau (boucle d'événements).
- **`poll` plutôt que `select`** : `select` ne gère pas les descripteurs au-delà de `FD_SETSIZE` (1024) et modifie ses ensembles à chaque appel.
- **Échéances absolues sur horloge monotone** (`clock_gettime(CLOCK_MONOTONIC)`) : chaque connexion a sa propre échéance, et un changement de l'heure système ne fausse pas les délais.
- **Suppression par échange avec le dernier élément** : O(1), sans décaler le tableau. Le parcours se fait à l'envers pour que l'élément déplacé ait déjà été traité, ce qui garde la correspondance entre `slots[i]` et `pfds[i]`.
- **Limite de descripteurs** : si `socket()` échoue avec `EMFILE` ou `ENFILE` alors que des connexions sont en cours, on attend qu'une place se libère au lieu d'abandonner le port. Vérifié avec `ulimit -n 64` et `-c 1000`.
- **`EINTR`** : un `poll` interrompu par un signal n'est pas une erreur, la boucle reprend simplement.

### Choisir le parallélisme

| `-c` | Effet |
|---|---|
| 1 | séquentiel, utile pour comparer ou ménager une cible fragile |
| 100 (défaut) | bon compromis pour un réseau local |
| 500 à 1000 | balayage rapide de tous les ports d'une machine locale |

Au-delà, le gain diminue : sur une cible qui répond vite, les connexions se terminent avant que le parallélisme ne serve, et trop de connexions simultanées peuvent saturer une petite machine ou déclencher ses protections.

## 5. Récupération de bannière

Pour chaque port ouvert, `main` rouvre une connexion avec `tcp_connect`, puis appelle `grab_banner` :

```
attendre des données (poll POLLIN, timeout_ms)
   │
   ├─ données reçues ──────────────────────────────┐
   │   (SSH, FTP, SMTP, POP3, IMAP parlent en       │
   │    premier)                                    │
   │                                                ▼
   └─ rien ─► envoyer "HEAD / HTTP/1.0\r\n\r\n"   réponse reçue
              attendre de nouveau                  │
                 │                                 ▼
               rien → bannière vide        commence par "HTTP/" ?
                                             oui → valeur de "Server:"
                                                   (ou ligne de statut)
                                             non → première ligne
                                                   │
                                                   ▼
                                         nettoyage : non imprimable → '.'
```

- **Pourquoi une sonde HTTP** : un serveur web ne dit rien tant qu'on ne lui envoie pas de requête. `HEAD` demande seulement les en-têtes, et HTTP/1.0 ferme la connexion après la réponse.
- **Nettoyage** : la bannière vient d'une machine potentiellement hostile. Afficher des octets bruts permettrait d'injecter des séquences d'échappement dans le terminal (changer les couleurs, effacer l'écran, voire pire sur certains émulateurs). Les octets nuls sont aussi remplacés dès la réception, sinon une bannière binaire serait tronquée par les fonctions de chaînes C.
- **`MSG_NOSIGNAL`** : écrire sur une connexion fermée par l'autre côté provoque normalement un signal `SIGPIPE` qui tue le processus. Ce drapeau le remplace par une simple erreur.
- **Durée** : au plus environ `2 × timeout_ms` par port ouvert (attente initiale + attente après la sonde).

## 6. Identification du service

`identify_service` combine deux sources :

1. **La bannière**, fiable car c'est le service qui se présente :

| Motif | Service |
|---|---|
| réponse HTTP (indicateur `is_http`) | `http` |
| commence par `SSH-` | `ssh` |
| commence par `220` et contient `SMTP` | `smtp` |
| commence par `220` et contient `FTP` | `ftp` |
| commence par `+OK` | `pop3` |
| commence par `* OK` | `imap` |
| commence par `RFB ` | `vnc` |

2. **Le numéro de port**, une simple supposition : `getservbyport` lit `/etc/services` (3306 → `mysql`). Le nom est alors suivi de `?`, comme dans nmap.

L'indicateur `is_http` est fourni par `grab_banner`, car une fois l'en-tête `Server:` extrait (`Apache/2.4.58`), on ne saurait plus que la réponse venait de HTTP.

Pour SSH, la version est extraite de la bannière : `SSH-2.0-OpenSSH_9.6p1 Ubuntu` donne `OpenSSH 9.6p1 Ubuntu` (préfixe de protocole retiré, premier `_` remplacé par un espace). Pour les autres services, la bannière nettoyée sert de version.

## 7. Garde-fou

Avant tout scan, `is_private_target` vérifie l'adresse **résolue** : un nom comme `exemple.com` est testé sur son IP réelle, pas sur son nom.

| Plage | Nom | Test sur l'IP en ordre machine |
|---|---|---|
| `127.0.0.0/8` | loopback | `ip >> 24 == 127` |
| `10.0.0.0/8` | privé (RFC 1918) | `ip >> 24 == 10` |
| `172.16.0.0/12` | privé | `ip >> 20 == (172 << 4 \| 1)` |
| `192.168.0.0/16` | privé | `ip >> 16 == (192 << 8 \| 168)` |
| `169.254.0.0/16` | lien local | `ip >> 16 == (169 << 8 \| 254)` |
| `::1` | loopback IPv6 | comparaison des 16 octets |
| `fc00::/7` | ULA (privé IPv6) | `(octet0 & 0xfe) == 0xfc` |
| `fe80::/10` | lien local IPv6 | `octet0 == 0xfe && (octet1 & 0xc0) == 0x80` |

Un masque `/n` signifie que les `n` premiers bits sont fixés : décaler l'adresse de `32 - n` bits vers la droite ne garde que ces bits, qu'on compare au préfixe. Par exemple `172.16.0.0/12` : les 12 premiers bits valent `1010 1100 0001`, soit `172 << 4 | 1`. C'est pour ça que `172.31.255.255` est accepté et `172.32.0.1` refusé.

Une adresse IPv4 encapsulée en IPv6 (`::ffff:8.8.8.8`) est reconnue par ses 12 premiers octets et testée avec les règles IPv4 ; sinon elle permettrait de contourner le contrôle.

Une cible refusée produit le code de sortie 2 et un message rappelant le cadre légal. `--allow-public` lève la restriction en affichant un avertissement.

## 8. Gestion des erreurs et des ressources

| Situation | Comportement |
|---|---|
| argument invalide | message sur `stderr`, code 1 |
| hôte introuvable | message de `gai_strerror`, code 1 |
| cible publique sans option | message, code 2 |
| échec d'allocation ou de `poll` | message, code 1, tous les sockets fermés |
| `socket()` impossible pour un port | état `PORT_ERROR` pour ce port, le scan continue |
| limite de descripteurs atteinte | attente d'une place libre |
| service qui ferme pendant l'envoi | erreur gérée grâce à `MSG_NOSIGNAL` |

Chaque socket est fermé dans la fonction qui l'a ouvert, sauf celui renvoyé par `tcp_connect`, que l'appelant doit fermer (c'est indiqué dans sa spécification). valgrind avec `--track-fds=yes` confirme qu'aucun descripteur ne reste ouvert à la fin.
