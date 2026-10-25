# Port-Scanner

Scanner de ports TCP écrit en C avec l'API sockets POSIX. Pour une machine et une plage de ports donnés, il indique quels ports sont **ouverts**, **fermés** ou **filtrés**, récupère la **bannière** des services qui répondent et en déduit le **service** et sa **version**.

> **Cadre légal.** Scanner une machine sans autorisation est interdit en France (article 323-1 du Code pénal) et dans la plupart des pays. Cet outil est destiné à vos propres machines ou à des environnements que vous avez l'autorisation de tester. Par défaut, il **refuse** toute cible en dehors des adresses locales et privées (voir [Garde-fou](#garde-fou)).

```
$ ./scanner 127.0.0.1 18000 18200
Scan de 127.0.0.1 (127.0.0.1), ports 18000-18200

PORT     ETAT     SERVICE      VERSION
18021    ouvert   ftp          220 (vsFTPd 3.0.5)
18022    ouvert   ssh          OpenSSH 9.6p1 Ubuntu-3ubuntu13
18025    ouvert   smtp         220 mail.example.test ESMTP Postfix
18030    ouvert   inconnu      ..binaire.
18031    ouvert   inconnu
18080    ouvert   http         FakeHTTP/1.0
18110    ouvert   pop3         +OK Dovecot ready.
18143    ouvert   imap         * OK [CAPABILITY IMAP4rev1] Dovecot ready.

8 port(s) ouvert(s), 0 filtre(s), 193 ferme(s) sur 201, en 0.91 s
```

## Sommaire

- [Compilation](#compilation)
- [Utilisation](#utilisation)
- [Fonctionnement](#fonctionnement)
- [Architecture](#architecture)
- [Tests](#tests)
- [Comparaison avec nmap](#comparaison-avec-nmap)
- [Limites et pistes d'amélioration](#limites-et-pistes-damélioration)

## Compilation

Prérequis : Linux (ou tout système POSIX), `gcc`, `make`. Python 3 pour les tests.

```bash
git clone https://github.com/RIZIWYX/Port-Scanner.git
cd Port-Scanner
make            # produit l'exécutable ./scanner
make test       # lance les tests automatiques
make clean      # supprime obj/ et l'exécutable
```

Le code est compilé en C11 strict avec `-Wall -Wextra -Werror` : aucun avertissement n'est toléré. Les fichiers objets sont placés dans `obj/`, et les dépendances aux headers sont suivies automatiquement (`-MMD -MP`) : modifier un `.h` recompile exactement les fichiers qui l'incluent.

## Utilisation

```
./scanner <hote> <port_debut> <port_fin> [-t timeout_ms] [-c parallele] [--allow-public]
```

| Argument | Rôle | Défaut |
|---|---|---|
| `hote` | nom ou adresse IPv4/IPv6 de la cible | |
| `port_debut`, `port_fin` | plage de ports scannée, bornes incluses (1 à 65535) | |
| `-t timeout_ms` | attente maximale d'une réponse par port, de 10 à 10000 ms | 500 |
| `-c parallele` | nombre de connexions simultanées, de 1 à 1000 (1 = séquentiel) | 100 |
| `--allow-public` | autorise une cible hors des plages locales et privées | désactivé |

Exemples :

```bash
./scanner 127.0.0.1 1 1024              # ports système de sa propre machine
./scanner localhost 1 65535 -c 500      # tous les ports, 500 connexions en parallèle
./scanner 192.168.1.10 20 443 -t 300    # une machine de son réseau local
./scanner ::1 1 1024                    # en IPv6
```

Codes de retour : `0` scan effectué, `1` arguments invalides, hôte introuvable ou erreur, `2` cible refusée par le garde-fou.

## Fonctionnement

### 1. État d'un port : le TCP connect scan

Pour chaque port, le scanner tente une connexion TCP complète avec `connect()`, qui réalise la poignée de main en trois temps. La réponse de la cible donne l'état du port :

| Réponse de la cible | État | Signification |
|---|---|---|
| SYN-ACK | **ouvert** | un programme écoute sur ce port |
| RST | **fermé** | la machine répond, mais personne n'écoute |
| aucune réponse dans le délai, ou « hôte injoignable » | **filtré** | un pare-feu bloque probablement les paquets ; impossible de conclure |

C'est la méthode `-sT` de nmap. Elle ne demande aucun droit administrateur, contrairement au scan SYN (`-sS`) qui forge des paquets bruts.

### 2. Délai maîtrisé : connexions non bloquantes

Un `connect()` classique est bloquant : face à un port filtré, Linux retransmet le SYN et n'abandonne qu'au bout de deux minutes environ. Le scanner place donc chaque socket en mode non bloquant (`fcntl` + `O_NONBLOCK`) :

1. `connect()` rend la main immédiatement avec `EINPROGRESS` ;
2. `poll()` attend que le socket devienne inscriptible, au plus `timeout_ms` ;
3. le vrai résultat de la connexion est lu avec `getsockopt(SO_ERROR)`.

### 3. Scan parallèle

Attendre chaque port l'un après l'autre coûte `nombre de ports × timeout` dans le pire cas. `scan_range` maintient jusqu'à `-c` connexions en cours **en même temps**, toutes surveillées par **un seul appel à `poll()`** :

- chaque connexion en cours a une échéance (instant de lancement + timeout, mesuré sur une horloge monotone) ;
- `poll()` attend jusqu'à l'échéance la plus proche ;
- toute connexion terminée ou expirée libère sa place pour le port suivant.

Ce modèle événementiel, le même que celui des serveurs réseau, évite de créer un thread par port. Si la limite de descripteurs du processus (`ulimit -n`) est atteinte, le scan attend qu'une place se libère au lieu d'échouer.

Mesures sur une adresse qui ne répond pas, timeout 300 ms :

| Ports filtrés | Séquentiel (`-c 1`) | Parallèle (défaut) |
|---|---|---|
| 20 | 6,01 s | 0,30 s |
| 200 | 60 s (calculé : 200 × 0,3 s) | 0,60 s |

### 4. Bannière

Une fois les ports ouverts connus, le scanner se reconnecte à chacun pour écouter le service :

1. il attend que le service parle en premier : c'est le cas de SSH, FTP, SMTP, POP3, IMAP ;
2. si rien n'arrive, il envoie une requête HTTP minimale (`HEAD / HTTP/1.0`), car un serveur web attend qu'on lui parle ;
3. pour une réponse HTTP, il garde l'en-tête `Server:`, sinon la première ligne reçue.

La bannière vient d'une machine extérieure : elle est **nettoyée** avant affichage (caractères non imprimables remplacés par `.`, longueur bornée) pour qu'un service malveillant ne puisse pas injecter de séquences de contrôle dans le terminal. L'envoi utilise `MSG_NOSIGNAL`, pour qu'un service qui ferme la connexion ne tue pas le scanner par un `SIGPIPE`.

### 5. Identification du service

1. **Par la bannière** (fiable) : `SSH-` → ssh, réponse HTTP → http, `220 … FTP` → ftp, `220 … SMTP` → smtp, `+OK` → pop3, `* OK` → imap, `RFB ` → vnc. Pour SSH, la version du logiciel est extraite (`SSH-2.0-OpenSSH_9.6p1` → `OpenSSH 9.6p1`).
2. **Par le numéro de port** (supposition) : `getservbyport` consulte `/etc/services`. Le nom est alors suivi d'un `?`, comme dans nmap, car rien n'empêche de faire tourner un serveur web sur le port 22.

### Garde-fou

Après la résolution du nom, l'adresse réelle est comparée aux plages non routées sur Internet :

- IPv4 : `127.0.0.0/8` (loopback), `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16` (RFC 1918), `169.254.0.0/16` (lien local) ;
- IPv6 : `::1`, `fc00::/7`, `fe80::/10`.

Les adresses IPv4 encapsulées en IPv6 (`::ffff:8.8.8.8`) sont vérifiées avec les règles IPv4, pour qu'on ne puisse pas contourner le contrôle. Le test porte sur l'IP résolue, pas sur le nom saisi. Toute autre cible est refusée (code 2), sauf avec `--allow-public`, qui affiche un avertissement.

## Architecture

```
Port-Scanner/
├── Makefile
├── include/          # interfaces : chaque fonction y est spécifiée
│   ├── cli.h
│   ├── net.h
│   ├── guard.h
│   ├── banner.h
│   └── service.h
├── src/
│   ├── main.c        # enchaîne les étapes et affiche le résultat
│   ├── cli.c         # lecture et validation des arguments
│   ├── net.c         # résolution, connexion non bloquante, scan parallèle
│   ├── guard.c       # restriction aux adresses locales et privées
│   ├── banner.c      # récupération et nettoyage de la bannière
│   └── service.c     # identification du service et de la version
└── tests/
    ├── run_tests.sh      # tests automatiques (make test)
    └── fake_services.py  # faux services sur 127.0.0.1
```

Un module = un `.c` et un `.h` du même nom. Chaque fonction, publique ou interne, est précédée de sa spécification : rôle, paramètres et contraintes, valeur de retour dans tous les cas, effets de bord.

```
main ─► cli : parse_args
     ─► net : resolve_host
     ─► guard : is_private_target
     ─► net : scan_range ────────► état de chaque port
     └─► pour chaque port ouvert :
           net : tcp_connect ─► banner : grab_banner ─► service : identify_service
```

## Tests

```bash
make test
```

Le script lance de faux services sur `127.0.0.1` (FTP, SSH, SMTP, POP3, IMAP, HTTP, un service muet et un service binaire), exécute le scanner et vérifie **25 cas** :

- chaque service est détecté et correctement identifié, avec sa version ;
- la bannière binaire est nettoyée, le service muet est bien vu ouvert ;
- le scan séquentiel (`-c 1`) et le scan parallèle donnent exactement le même résultat ;
- la résolution de `localhost` et IPv6 (`::1`) ;
- le garde-fou refuse `8.8.8.8`, `::ffff:8.8.8.8` et `172.32.0.1` ;
- dix cas d'arguments invalides sont rejetés avec le code 1.

Le programme a aussi été vérifié avec **valgrind** (`--leak-check=full --track-fds=yes`) : aucune erreur mémoire, aucune fuite, aucun descripteur laissé ouvert.

## Comparaison avec nmap

Même cible, mêmes faux services (`python3 tests/fake_services.py`) :

```bash
./scanner 127.0.0.1 18000 18200
nmap -sT -sV -p 18000-18200 127.0.0.1
```

| Port | Ce scanner | nmap 7.94 `-sT -sV` |
|---|---|---|
| 18021 | ftp, `220 (vsFTPd 3.0.5)` | ftp, vsftpd 3.0.5 |
| 18022 | ssh, `OpenSSH 9.6p1 Ubuntu-3ubuntu13` | ssh, OpenSSH 9.6p1 Ubuntu 3ubuntu13 |
| 18025 | smtp, `220 mail.example.test ESMTP Postfix` | smtp, Postfix smtpd |
| 18030 | inconnu, `..binaire.` | unknown |
| 18031 | inconnu (muet) | tcpwrapped |
| 18080 | http, `FakeHTTP/1.0` | unknown |
| 18110 | pop3, `+OK Dovecot ready.` | pop3, Dovecot pop3d |
| 18143 | imap, `* OK [CAPABILITY ...] Dovecot ready.` | imap, Dovecot imapd |
| autres | 193 fermés | 193 fermés |

**Les deux outils trouvent exactement les mêmes 8 ports ouverts et 193 fermés.** Sur l'identification :

- pour FTP, SSH, SMTP, POP3 et IMAP, les deux concluent au même service ; nmap normalise en plus le nom du logiciel grâce à sa base de signatures ;
- sur le serveur HTTP de test, ce scanner reconnaît le protocole HTTP, alors que nmap répond `unknown` car il ne trouve pas `FakeHTTP` dans sa base ;
- nmap donne plus d'informations (système d'exploitation, version de protocole) grâce à ses sondes multiples.

Durée sur les 65 535 ports de localhost : **3,2 s** pour ce scanner (`-c 1000`, récupération des bannières comprise) contre **1,8 s** pour `nmap -sT -p- -T4` sans détection de version.

## Limites et pistes d'amélioration

- **TCP uniquement** : pas de scan UDP, qui demande des sondes propres à chaque protocole.
- **TCP connect** : chaque port ouvert produit une connexion complète, visible dans les journaux de la cible. Un scan SYN (connexion jamais terminée) demanderait des sockets bruts et les droits root.
- **Une seule adresse** : seule la première adresse renvoyée par `getaddrinfo` est scannée, pas de plages CIDR.
- **Bannières récupérées en séquentiel** : seul le scan des états est parallèle. Un service muet coûte jusqu'à `2 × timeout` ; les bannières pourraient être lues avec le même `poll()` que le scan.
- **Identification par motifs simples** : pas de TLS, donc HTTPS ou SMTPS restent « inconnus » ou devinés par le port.
- **Pistes** : sortie JSON ou CSV, sondes supplémentaires (Redis, MySQL), liste de ports (`-p 22,80,443`), affichage de la progression.

## Licence

MIT, voir [LICENSE](LICENSE).
