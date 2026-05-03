#ifndef VITA_RESOLVE_H
#define VITA_RESOLVE_H
#ifdef __PSVITA__
#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>

/* Resolve hostname to AF_INET sockaddr_in via Sony's native resolver.
 * Fills out->sin_family, out->sin_port (network byte order), and out->sin_addr.
 * Returns true on success. */
bool vita_resolve_sin(const char *hostname, uint16_t port, struct sockaddr_in *out);

#endif /* __PSVITA__ */
#endif /* VITA_RESOLVE_H */
