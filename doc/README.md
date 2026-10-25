# Documentation technique

Documentation interne du scanner de ports. Pour l'installation et l'utilisation, voir le [README principal](../README.md).

| Document | Contenu | Pour qui |
|---|---|---|
| [architecture.md](architecture.md) | modules, dépendances, flux de données, compilation | comprendre l'organisation du code |
| [fonctionnement.md](fonctionnement.md) | rappels réseau, algorithmes de chaque étape, choix techniques | comprendre comment et pourquoi ça marche |
| [api.md](api.md) | référence de chaque type et de chaque fonction | utiliser ou modifier un module |
| [tests.md](tests.md) | stratégie de test, liste des cas, valgrind, comparaison nmap | vérifier ou étendre les tests |
| [historique.md](historique.md) | étapes du développement, décisions, bugs rencontrés | suivre l'évolution du projet |

## Le projet en bref

- **Langage** : C11, API sockets POSIX, aucune bibliothèque externe.
- **Plateforme** : Linux (testé sous Ubuntu), tout système POSIX en principe.
- **Méthode** : TCP connect scan non bloquant et parallèle, puis lecture de bannière.
- **Taille** : 6 modules, environ 1 000 lignes de C commentées (sources et headers), 26 tests automatiques.
- **Sécurité** : cibles limitées aux adresses locales et privées par défaut, bannières nettoyées avant affichage.

## Glossaire

| Terme | Définition |
|---|---|
| **Port** | numéro de 1 à 65535 qui désigne, sur une machine, le programme destinataire d'une connexion. |
| **Socket** | point de communication fourni par le noyau, manipulé par un descripteur de fichier (un entier). |
| **Descripteur (fd)** | entier qui référence une ressource ouverte (fichier, socket) dans un processus. Leur nombre est limité (`ulimit -n`). |
| **SYN / SYN-ACK / ACK** | les trois paquets de la poignée de main qui ouvre une connexion TCP. |
| **RST** | paquet de refus envoyé quand aucun programme n'écoute sur le port. |
| **Port ouvert** | un programme écoute : la connexion aboutit. |
| **Port fermé** | personne n'écoute : la machine répond par un RST. |
| **Port filtré** | aucune réponse ou cible injoignable : un pare-feu bloque probablement, l'état réel est inconnu. |
| **Non bloquant** | mode d'un socket où les appels rendent la main immédiatement au lieu d'attendre. |
| **`poll`** | appel système qui attend, avec un délai maximal, qu'un ou plusieurs descripteurs soient prêts. |
| **Bannière** | texte envoyé par un service, qui permet souvent de l'identifier (ex. `SSH-2.0-OpenSSH_9.6`). |
| **Ordre réseau** | ordre des octets *big-endian* imposé sur le réseau ; `htons` / `ntohl` font la conversion. |
