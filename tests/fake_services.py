#!/usr/bin/env python3
"""Faux services TCP sur 127.0.0.1 (et ::1 si IPv6 est disponible),
utilises par run_tests.sh.

Chaque service imite la banniere d'un vrai protocole, pour verifier que le
scanner detecte le port et identifie le service sans avoir a installer de
vrais serveurs. Le script tourne jusqu'a ce qu'on le tue.
"""
import socket
import sys
import threading
import time

# port -> banniere envoyee a la connexion (None : service muet)
SERVICES = {
    18021: b"220 (vsFTPd 3.0.5)\r\n",
    18022: b"SSH-2.0-OpenSSH_9.6p1 Ubuntu-3ubuntu13\r\n",
    18025: b"220 mail.example.test ESMTP Postfix\r\n",
    18030: b"\x00\x01binaire\xff\r\n",
    18031: None,
    18110: b"+OK Dovecot ready.\r\n",
    18143: b"* OK [CAPABILITY IMAP4rev1] Dovecot ready.\r\n",
}

HTTP_PORT = 18080
HTTP_RESPONSE = (b"HTTP/1.0 200 OK\r\nServer: FakeHTTP/1.0\r\n"
                 b"Content-Length: 0\r\n\r\n")


def listen(port, family=socket.AF_INET, addr="127.0.0.1"):
    s = socket.socket(family, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((addr, port))
    s.listen(64)
    return s


def handle(conn, banner):
    try:
        if banner is not None:
            conn.sendall(banner)
        else:
            time.sleep(2)       # service muet : garde la connexion ouverte
    except OSError:
        pass
    finally:
        conn.close()


def handle_http(conn):
    try:
        conn.settimeout(10)
        if conn.recv(1024):     # repond seulement apres une requete
            conn.sendall(HTTP_RESPONSE)
    except OSError:
        pass
    finally:
        conn.close()


def serve(sock, handler, *args):
    while True:
        conn, _ = sock.accept()
        threading.Thread(target=handler, args=(conn, *args),
                         daemon=True).start()


def main():
    try:
        sockets = [(listen(p), handle, b) for p, b in SERVICES.items()]
        sockets.append((listen(HTTP_PORT), handle_http))
    except OSError as e:
        print(f"fake_services: {e}", file=sys.stderr)
        sys.exit(1)
    # Memes services sur ::1 : sur certaines machines (dont GitHub Actions),
    # "localhost" se resout d'abord en ::1. Ignore si IPv6 est indisponible.
    try:
        sockets += [(listen(p, socket.AF_INET6, "::1"), handle, b)
                    for p, b in SERVICES.items()]
        sockets.append((listen(HTTP_PORT, socket.AF_INET6, "::1"), handle_http))
    except OSError:
        pass
    for sock, handler, *args in sockets:
        threading.Thread(target=serve, args=(sock, handler, *args),
                         daemon=True).start()
    print("pret", flush=True)
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
