# Tests et validation

Trois niveaux de vérification : des **tests automatiques** sur des services simulés, un **contrôle mémoire** avec valgrind, et une **comparaison avec nmap**, l'outil de référence.

## 1. Tests automatiques

```bash
make test        # ou : bash tests/run_tests.sh
```

### Principe

Les tests ne doivent dépendre ni du réseau ni des services installés sur la machine. `tests/fake_services.py` lance donc sur `127.0.0.1` des **faux services** qui imitent la bannière de vrais protocoles :

| Port | Simule | Comportement |
|---|---|---|
| 18021 | FTP (vsftpd) | envoie `220 (vsFTPd 3.0.5)` |
| 18022 | SSH (OpenSSH) | envoie `SSH-2.0-OpenSSH_9.6p1 Ubuntu-3ubuntu13` |
| 18025 | SMTP (Postfix) | envoie `220 mail.example.test ESMTP Postfix` |
| 18030 | service binaire | envoie des octets nuls et non imprimables |
| 18031 | service muet | accepte la connexion et ne dit rien |
| 18080 | HTTP | ne répond qu'après une requête, avec `Server: FakeHTTP/1.0` |
| 18110 | POP3 (Dovecot) | envoie `+OK Dovecot ready.` |
| 18143 | IMAP (Dovecot) | envoie `* OK [CAPABILITY IMAP4rev1] Dovecot ready.` |

Les ports 18000 à 18200 sont hors des plages habituelles pour ne pas entrer en conflit avec un vrai service. Le script attend que les faux services soient prêts, lance les tests, puis les arrête (`trap ... EXIT`, même si un test échoue).

### Liste des cas

**Détection et identification** (scan parallèle de 18000 à 18200)

| Test | Vérifie |
|---|---|
| ftp détecté | bannière lue et service reconnu par `220 ... FTP` |
| ssh et version | version extraite : `OpenSSH 9.6p1` |
| smtp détecté | `220 ... SMTP` reconnu comme smtp et non ftp |
| bannière binaire nettoyée | octets nuls et non imprimables remplacés par `.` |
| service muet ouvert | un service silencieux est vu ouvert, bannière vide |
| http via sonde | la sonde `HEAD` est envoyée et l'en-tête `Server:` extrait |
| pop3, imap détectés | motifs `+OK` et `* OK` |
| port fermé non affiché | seuls les ports ouverts sont listés |
| bilan | exactement 8 ouverts, 0 filtré, 193 fermés sur 201 |

**Cohérence**

| Test | Vérifie |
|---|---|
| mêmes résultats avec `-c 1` | le scan séquentiel et le scan parallèle donnent les mêmes lignes |
| localhost résolu | la résolution de nom fonctionne |
| `::1` accepté | IPv6 (test ignoré si la machine ne supporte pas IPv6) |

**Garde-fou** (code de sortie 2, aucun paquet envoyé)

`8.8.8.8`, `::ffff:8.8.8.8` (IPv4 déguisée en IPv6) et `172.32.0.1` (juste en dehors de `172.16.0.0/12`).

**Arguments invalides** (code de sortie 1)

Trop peu d'arguments, plage inversée, port 0, port 65536, port non numérique, timeout trop petit, `-t` sans valeur, parallélisme 0, option inconnue, hôte introuvable.

### Résultat attendu

```
Resultat : 26 reussi(s), 0 echoue(s)
```
(25 sur une machine sans IPv6.) Le script renvoie un code non nul si un test échoue, ce qui permet de l'utiliser dans une intégration continue.

### Ajouter un test

- **Nouveau service** : ajouter une entrée `port: bannière` dans `SERVICES` de `fake_services.py`, puis une ligne dans `run_tests.sh` :
  ```bash
  check "redis detecte (18079)" line_matches '^18079 +ouvert +redis '
  ```
  et mettre à jour le bilan attendu (nombre de ports ouverts).
- **Nouveau cas d'erreur** :
  ```bash
  check "description" exit_code <code attendu> <arguments...>
  ```

## 2. Contrôle mémoire

```bash
valgrind --leak-check=full --track-fds=yes ./scanner 127.0.0.1 18000 18200
```

| Vérification | Résultat |
|---|---|
| erreurs mémoire (lectures ou écritures invalides) | 0 |
| fuites mémoire | 0 (les tableaux de `scan_range` et `states` sont libérés) |
| descripteurs ouverts à la fin | 3 (seulement stdin, stdout, stderr) |

`--track-fds` est important ici : un socket oublié ne provoque pas d'erreur visible sur un petit scan, mais ferait échouer un scan de 65 535 ports.

## 3. Tests de robustesse

| Situation | Commande | Résultat |
|---|---|---|
| limite de descripteurs très basse | `(ulimit -n 64; ./scanner 127.0.0.1 1 300 -c 1000)` | le scan se termine normalement, plus lentement |
| tous les ports | `./scanner 127.0.0.1 1 65535 -c 1000` | environ 3 s, bannières comprises |
| cible muette | `./scanner 10.255.255.1 1 20 -t 300` | 20 filtrés en 0,30 s (6,01 s avec `-c 1`) |
| pare-feu local | `sudo iptables -A INPUT -p tcp --dport 8080 -j DROP` puis scan du 8080 | port vu filtré alors que le service tourne (retirer la règle avec `-D`) |

## 4. Comparaison avec nmap

Même cible, mêmes faux services :

```bash
python3 tests/fake_services.py &
./scanner 127.0.0.1 18000 18200
nmap -sT -sV -p 18000-18200 127.0.0.1
```

| Port | Ce scanner | nmap 7.94 `-sT -sV` | Accord |
|---|---|---|---|
| 18021 | ftp, `220 (vsFTPd 3.0.5)` | ftp, vsftpd 3.0.5 | oui |
| 18022 | ssh, `OpenSSH 9.6p1 Ubuntu-3ubuntu13` | ssh, OpenSSH 9.6p1 Ubuntu 3ubuntu13 | oui |
| 18025 | smtp | smtp, Postfix smtpd | oui |
| 18030 | inconnu, `..binaire.` | unknown | oui |
| 18031 | inconnu | tcpwrapped | oui (aucun des deux ne conclut) |
| 18080 | http, `FakeHTTP/1.0` | unknown | non : le scanner reconnaît HTTP, nmap ne trouve pas de signature |
| 18110 | pop3 | pop3, Dovecot pop3d | oui |
| 18143 | imap | imap, Dovecot imapd | oui |
| autres | 193 fermés | 193 fermés | oui |

**États des ports : accord total.** Les différences portent sur l'identification :

- nmap envoie de nombreuses sondes et compare les réponses à une base de plus de 10 000 signatures : il normalise le nom du logiciel (`Postfix smtpd`), détecte le système, la version de protocole ;
- ce scanner reconnaît un protocole par des motifs simples : moins précis, mais il identifie HTTP sur un serveur inconnu de nmap.

**Durée sur les 65 535 ports de localhost** : 3,2 s pour ce scanner (`-c 1000`, bannières comprises) contre 1,8 s pour `nmap -sT -p- -T4` sans détection de version.
