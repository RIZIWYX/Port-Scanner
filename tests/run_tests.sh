#!/usr/bin/env bash
# Tests automatiques du scanner, uniquement sur 127.0.0.1.
# Usage : bash tests/run_tests.sh   (ou make test)

cd "$(dirname "$0")/.." || exit 1

SCANNER=./scanner
PASS=0
FAIL=0

ok()   { echo "  OK      $1"; PASS=$((PASS + 1)); }
fail() { echo "  ECHEC   $1"; FAIL=$((FAIL + 1)); }

# check <description> <condition...> : OK si la condition est vraie
check() {
  local desc=$1
  shift
  if "$@"; then ok "$desc"; else fail "$desc"; fi
}

# exit_code <code attendu> <arguments du scanner...>
exit_code() {
  local expected=$1
  shift
  $SCANNER "$@" >/dev/null 2>&1
  [ $? -eq "$expected" ]
}

# line_matches <motif> : le dernier resultat contient une ligne qui correspond
line_matches() { grep -Eq "$1" <<< "$OUT"; }

echo "== Compilation"
make -s || { echo "La compilation a echoue"; exit 1; }

echo "== Demarrage des faux services"
python3 tests/fake_services.py > /tmp/fake_services.$$ 2>&1 &
FAKE_PID=$!
trap 'kill $FAKE_PID 2>/dev/null' EXIT
for _ in $(seq 50); do
  grep -q pret /tmp/fake_services.$$ 2>/dev/null && break
  sleep 0.1
done
if ! grep -q pret /tmp/fake_services.$$; then
  cat /tmp/fake_services.$$
  echo "Impossible de lancer les faux services"
  exit 1
fi
rm -f /tmp/fake_services.$$

echo "== Detection et identification (scan parallele)"
OUT=$($SCANNER 127.0.0.1 18000 18200 -t 300)
check "ftp detecte (18021)"          line_matches '^18021 +ouvert +ftp +220 \(vsFTPd 3\.0\.5\)'
check "ssh et version (18022)"       line_matches '^18022 +ouvert +ssh +OpenSSH 9\.6p1'
check "smtp detecte (18025)"         line_matches '^18025 +ouvert +smtp '
check "banniere binaire nettoyee"    line_matches '^18030 +ouvert +inconnu +\.\.binaire\.$'
check "service muet ouvert (18031)"  line_matches '^18031 +ouvert +inconnu *$'
check "http via sonde (18080)"       line_matches '^18080 +ouvert +http +FakeHTTP/1\.0$'
check "pop3 detecte (18110)"         line_matches '^18110 +ouvert +pop3 '
check "imap detecte (18143)"         line_matches '^18143 +ouvert +imap '
check "port ferme non affiche"       bash -c "! grep -q '^18099 ' <<< \"\$0\"" "$OUT"
check "bilan : 8 ouverts sur 201"    line_matches '^8 port\(s\) ouvert\(s\), 0 filtre\(s\), 193 ferme\(s\) sur 201'

echo "== Scan sequentiel identique au scan parallele"
SEQ=$($SCANNER 127.0.0.1 18000 18200 -t 300 -c 1 | grep ' ouvert ')
PAR=$(grep ' ouvert ' <<< "$OUT")
check "memes ports et services avec -c 1" [ "$SEQ" = "$PAR" ]

echo "== IPv6 et resolution de nom"
check "localhost resolu"  bash -c "$SCANNER localhost 18022 18022 | grep -q '^18022 .*ssh'"
if python3 -c 'import socket; s=socket.socket(socket.AF_INET6); s.bind(("::1",0))' 2>/dev/null; then
  check "::1 accepte" exit_code 0 ::1 1 10 -t 50
else
  echo "  (IPv6 indisponible, test ignore)"
fi

echo "== Garde-fou"
check "8.8.8.8 refuse (code 2)"         exit_code 2 8.8.8.8 80 80
check "::ffff:8.8.8.8 refuse (code 2)"  exit_code 2 ::ffff:8.8.8.8 80 80
check "172.32.0.1 refuse (hors 172.16/12)" exit_code 2 172.32.0.1 80 80

echo "== Arguments invalides (code 1)"
check "trop peu d'arguments"   exit_code 1 127.0.0.1 80
check "plage inversee"         exit_code 1 127.0.0.1 80 20
check "port 0"                 exit_code 1 127.0.0.1 0 10
check "port 65536"             exit_code 1 127.0.0.1 1 65536
check "port non numerique"     exit_code 1 127.0.0.1 1 80abc
check "timeout trop petit"     exit_code 1 127.0.0.1 1 10 -t 5
check "-t sans valeur"         exit_code 1 127.0.0.1 1 10 -t
check "parallelisme 0"         exit_code 1 127.0.0.1 1 10 -c 0
check "option inconnue"        exit_code 1 127.0.0.1 1 10 -x
check "hote introuvable"       exit_code 1 hote.inexistant.invalid 1 10

echo
echo "Resultat : $PASS reussi(s), $FAIL echoue(s)"
[ "$FAIL" -eq 0 ]
