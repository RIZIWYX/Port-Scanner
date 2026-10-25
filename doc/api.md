# Référence de l'API

Toutes les fonctions et tous les types du projet, module par module. La spécification de référence reste le commentaire au-dessus de chaque déclaration dans le code ; ce document en donne une vue d'ensemble.

Notation : **publique** = déclarée dans un header, utilisable par les autres modules ; **interne** = `static`, visible seulement dans son fichier.

- [cli](#module-cli) · [net](#module-net) · [guard](#module-guard) · [banner](#module-banner) · [service](#module-service) · [main](#module-main)

---

## Module `cli`

`include/cli.h`, `src/cli.c` : lecture et validation de la ligne de commande.

### `scan_config` (type public)

```c
typedef struct {
  const char *host;
  int port_start;
  int port_end;
  int timeout_ms;
  int parallel;
  int allow_public;
} scan_config;
```

| Champ | Contrainte | Défaut |
|---|---|---|
| `host` | nom ou IP texte ; pointe vers `argv`, ne pas libérer | obligatoire |
| `port_start` | [1, 65535] | obligatoire |
| `port_end` | [`port_start`, 65535] | obligatoire |
| `timeout_ms` | [10, 10000] | 500 |
| `parallel` | [1, 1000] | 100 |
| `allow_public` | 0 ou 1 | 0 |

### `parse_args` (publique)

```c
int parse_args(int argc, char **argv, scan_config *cfg);
```

Analyse `<hote> <port_debut> <port_fin> [-t ms] [-c n] [--allow-public]`. Les options peuvent être dans n'importe quel ordre après les trois arguments obligatoires.

- **Retour** : `0` si tout est valide (`*cfg` entièrement rempli) ; `-1` sinon, avec un message sur `stderr`.
- **Cas d'erreur** : trop peu d'arguments, port hors bornes ou non numérique, plage inversée, option inconnue, `-t` ou `-c` sans valeur ou hors bornes.
- **Ne vérifie pas** l'hôte : c'est le rôle de `resolve_host`.

### `usage` (interne)

```c
static void usage(const char *prog);
```
Affiche la syntaxe d'appel sur `stderr`.

### `parse_int` (interne)

```c
static int parse_int(const char *s, int min, int max, int *out);
```
Convertit `s` en entier avec `strtol` et vérifie qu'il est dans [`min`, `max`]. Refuse la chaîne vide et les caractères en trop (`"80abc"`). Retour `0` (valeur dans `*out`) ou `-1` (`*out` inchangé). Préférée à `atoi`, qui renvoie 0 sans signaler d'erreur.

---

## Module `net`

`include/net.h`, `src/net.c` : résolution, connexion TCP, scan.

### `port_state` (type public)

```c
typedef enum { PORT_OPEN, PORT_CLOSED, PORT_FILTERED, PORT_ERROR } port_state;
```

| Valeur | Signification |
|---|---|
| `PORT_OPEN` | connexion aboutie (SYN-ACK), un service écoute |
| `PORT_CLOSED` | connexion refusée (RST) |
| `PORT_FILTERED` | pas de réponse dans le délai, ou cible injoignable |
| `PORT_ERROR` | erreur locale, état inconnu |

### `resolve_host` (publique)

```c
int resolve_host(const char *host, struct sockaddr_storage *addr, socklen_t *len);
```
Résout un nom ou une IP texte (IPv4 ou IPv6) avec `getaddrinfo`.

- **Retour** : `0`, `*addr` contient la première adresse trouvée (port à 0) et `*len` sa taille ; `-1` si la résolution échoue (message sur `stderr`, `*addr` et `*len` inchangés).

### `tcp_connect` (publique)

```c
int tcp_connect(const struct sockaddr_storage *addr, socklen_t len,
    int port, int timeout_ms, port_state *state);
```
Ouvre une connexion TCP non bloquante vers `port`, en attendant au plus `timeout_ms`.

- **Retour** : le descripteur du socket connecté si `*state == PORT_OPEN` ; **l'appelant doit le fermer**. Il reste en mode non bloquant. Sinon `-1`, et aucun socket ne reste ouvert.
- `*addr` n'est pas modifiée (le port est placé dans une copie).

### `scan_port` (publique)

```c
port_state scan_port(const struct sockaddr_storage *addr, socklen_t len,
    int port, int timeout_ms);
```
Teste un seul port : `tcp_connect` puis fermeture immédiate. Durée au plus `timeout_ms` environ. Conservée pour tester un port isolé ; le programme principal utilise `scan_range`.

### `scan_range` (publique)

```c
int scan_range(const struct sockaddr_storage *addr, socklen_t len,
    int port_start, int port_end, int timeout_ms, int max_parallel,
    port_state *states);
```
Scanne une plage avec jusqu'à `max_parallel` connexions simultanées, surveillées par un seul `poll`.

- **`states`** : au moins `port_end - port_start + 1` cases ; `states[p - port_start]` reçoit l'état du port `p`.
- **Retour** : `0` si tous les ports ont été scannés ; `-1` en cas d'échec d'allocation ou de `poll` (`states` incomplet).
- **Garanties** : tous les sockets sont fermés avant le retour ; si la limite de descripteurs est atteinte, le scan attend une place libre.
- **Durée** : environ `(nombre de ports / max_parallel) × timeout_ms` dans le pire cas.

### `state_str` (publique)

```c
const char *state_str(port_state s);
```
Libellé d'un état : `"ouvert"`, `"ferme"`, `"filtre"` ou `"erreur"`. Chaîne constante, à ne pas modifier ni libérer.

### Fonctions internes de `net`

| Fonction | Rôle |
|---|---|
| `static void set_port(struct sockaddr_storage *addr, int port)` | écrit le port (converti par `htons`) dans une adresse IPv4 ou IPv6 |
| `static port_state error_to_state(int err)` | traduit un code `errno` / `SO_ERROR` en état : `0` → ouvert, `ECONNREFUSED` → fermé, `EHOSTUNREACH` / `ENETUNREACH` / `ETIMEDOUT` → filtré, autre → erreur |
| `static int start_connect(base, len, port, int *fd, port_state *state, int *err)` | crée le socket, le passe en non bloquant, lance `connect`. Retour `1` : en cours (`*fd` ouvert) ; `0` : résultat immédiat dans `*state` (`*fd` ouvert seulement si ouvert) ; `-1` : socket impossible, cause dans `*err` |
| `static port_state finish_connect(int fd)` | lit le résultat d'une connexion terminée avec `getsockopt(SO_ERROR)` |
| `static long long now_ms(void)` | temps en millisecondes sur l'horloge monotone |
| `struct pending { int fd; int port; long long deadline; }` | connexion en cours dans `scan_range` |

---

## Module `guard`

`include/guard.h`, `src/guard.c` : restriction des cibles.

### `is_private_target` (publique)

```c
int is_private_target(const struct sockaddr_storage *addr);
```
- **Retour** : `1` si l'adresse est dans `127.0.0.0/8`, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `169.254.0.0/16`, `::1`, `fc00::/7`, `fe80::/10`, ou une IPv4 privée encapsulée (`::ffff:a.b.c.d`) ; `0` sinon, y compris pour une famille inconnue.

### `addr_to_str` (publique)

```c
const char *addr_to_str(const struct sockaddr_storage *addr, char *buf, size_t size);
```
Écrit l'IP en texte avec `inet_ntop`. `INET6_ADDRSTRLEN` (46) octets suffisent toujours. Retour : `buf`, qui contient `"?"` si la conversion échoue.

### `ipv4_is_private` (interne)

```c
static int ipv4_is_private(uint32_t ip);
```
Applique les règles IPv4 à une adresse en ordre machine (après `ntohl`). Retour `1` ou `0`.

---

## Module `banner`

`include/banner.h`, `src/banner.c` : récupération de bannière.

### `grab_banner` (publique)

```c
size_t grab_banner(int fd, int timeout_ms, char *buf, size_t size, int *is_http);
```
Lit la bannière du service joint par `fd` :
1. attend jusqu'à `timeout_ms` que le service parle en premier ;
2. sinon envoie `HEAD / HTTP/1.0` et attend de nouveau ;
3. réponse HTTP → valeur de `Server:` (ou ligne de statut) ; sinon première ligne ;
4. caractères non imprimables remplacés par `.`.

- **`fd`** : socket connecté, bloquant ou non ; **pas fermé** par la fonction.
- **`buf` / `size`** : reçoit la bannière terminée par `'\0'`, tronquée si besoin ; `size >= 2`.
- **`is_http`** : si non `NULL`, reçoit `1` si la réponse était du HTTP.
- **Retour** : longueur de la bannière, `0` si le service n'a rien envoyé.
- **Durée** : au plus environ `2 × timeout_ms`.

### Fonctions internes de `banner`

| Fonction | Rôle |
|---|---|
| `static size_t recv_timeout(int fd, int timeout_ms, char *buf, size_t size)` | attend des données (`poll` `POLLIN`) puis les lit ; remplace les octets nuls par `.` ; retour : octets lus, `0` si rien, connexion fermée ou erreur |
| `static size_t copy_line(const char *src, char *dst, size_t size)` | copie une ligne sans son saut de ligne, en nettoyant les caractères non imprimables ; retour : longueur écrite |
| `static const char *http_server(const char *resp)` | pointeur vers la valeur de l'en-tête `Server:` (insensible à la casse), ou `NULL` |

---

## Module `service`

`include/service.h`, `src/service.c` : identification du service.

### `identify_service` (publique)

```c
void identify_service(int port, const char *banner, int is_http,
    char *name, size_t name_size, char *version, size_t ver_size);
```
- **`name`** : service reconnu par la bannière (`ssh`, `http`, `ftp`, `smtp`, `pop3`, `imap`, `vnc`) ; sinon nom tiré de `/etc/services` suivi de `?` ; sinon `"inconnu"`.
- **`version`** : pour SSH, partie logiciel extraite (`OpenSSH 9.6p1`) ; sinon la bannière ; vide si la bannière est vide.
- Chaînes toujours terminées par `'\0'` et tronquées si nécessaire.

### Fonctions internes de `service`

| Fonction | Rôle |
|---|---|
| `static int contains_icase(const char *haystack, const char *needle)` | `1` si `needle` apparaît dans `haystack`, sans tenir compte de la casse |
| `static const char *service_from_banner(const char *banner, int is_http)` | nom du protocole reconnu dans la bannière, ou `NULL` |
| `static void ssh_version(const char *banner, char *out, size_t size)` | `SSH-2.0-OpenSSH_9.6p1 Ubuntu` → `OpenSSH 9.6p1 Ubuntu` |

---

## Module `main`

`src/main.c` : point d'entrée.

### `main`

```c
int main(int argc, char **argv);
```
Enchaîne `parse_args`, `resolve_host`, `is_private_target`, `scan_range`, puis la bannière et l'identification de chaque port ouvert, et affiche le bilan.

| Code de sortie | Signification |
|---|---|
| `0` | scan effectué |
| `1` | arguments invalides, hôte introuvable, échec d'allocation ou du scan |
| `2` | cible refusée par le garde-fou |

### Fonctions internes de `main`

| Fonction | Rôle |
|---|---|
| `static double elapsed_s(const struct timespec *start)` | secondes écoulées depuis `start` (horloge monotone) |
| `static void print_open_port(addr, len, int port, int timeout_ms)` | se reconnecte au port, récupère la bannière, identifie le service et affiche la ligne ; si le port s'est fermé entre-temps, l'affiche sans bannière |
