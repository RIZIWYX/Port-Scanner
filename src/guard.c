#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "guard.h"

/*
 * ipv4_is_private : applique les regles IPv4 a une adresse en ordre
 * machine (ex. 127.0.0.1 = 0x7F000001).
 * Retour : 1 si l'adresse est loopback, privee ou lien local, 0 sinon.
 */
static int ipv4_is_private(uint32_t ip) {
  return (ip >> 24) == 127                   /* 127.0.0.0/8    */
      || (ip >> 24) == 10                    /* 10.0.0.0/8     */
      || (ip >> 20) == ((172u << 4) | 1)     /* 172.16.0.0/12  */
      || (ip >> 16) == ((192u << 8) | 168)   /* 192.168.0.0/16 */
      || (ip >> 16) == ((169u << 8) | 254);  /* 169.254.0.0/16 */
}

int is_private_target(const struct sockaddr_storage *addr) {
  if (addr->ss_family == AF_INET) {
    const struct sockaddr_in *a4 = (const struct sockaddr_in *) addr;
    return ipv4_is_private(ntohl(a4->sin_addr.s_addr));
  }
  if (addr->ss_family == AF_INET6) {
    const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *) addr;
    const uint8_t *b = a6->sin6_addr.s6_addr;
    static const uint8_t loopback[16] = { [15] = 1 };
    static const uint8_t v4mapped[12] = { [10] = 0xff, [11] = 0xff };
    if (memcmp(b, loopback, 16) == 0) {
      return 1;                                      /* ::1       */
    }
    if (memcmp(b, v4mapped, 12) == 0) {              /* ::ffff:v4 */
      uint32_t ip = ((uint32_t) b[12] << 24) | ((uint32_t) b[13] << 16)
          | ((uint32_t) b[14] << 8) | b[15];
      return ipv4_is_private(ip);
    }
    return (b[0] & 0xfe) == 0xfc                     /* fc00::/7  */
        || (b[0] == 0xfe && (b[1] & 0xc0) == 0x80);  /* fe80::/10 */
  }
  return 0;
}

const char *addr_to_str(const struct sockaddr_storage *addr, char *buf,
    size_t size) {
  const void *src;
  if (addr->ss_family == AF_INET) {
    src = &((const struct sockaddr_in *) addr)->sin_addr;
  } else if (addr->ss_family == AF_INET6) {
    src = &((const struct sockaddr_in6 *) addr)->sin6_addr;
  } else {
    src = NULL;
  }
  if (src == NULL || inet_ntop(addr->ss_family, src, buf, size) == NULL) {
    strncpy(buf, "?", size);
  }
  return buf;
}
