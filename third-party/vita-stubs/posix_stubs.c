/*
 * POSIX function stubs for VitaSDK
 *
 * miniupnpc references these functions in code paths that are never
 * reached on Vita (debug logging in miniwget, error printing in
 * connecthostport).  Providing no-op stubs satisfies the linker
 * without pulling in missing POSIX symbols.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <stddef.h>
#include "netdb.h"

/* in6addr_any: declared as extern in net/if.h; definition lives here to avoid
 * multiple-definition errors when net/if.h is included by multiple translation
 * units (static definitions in headers are technically valid but waste space
 * and can confuse some linkers). */
const struct in6_addr in6addr_any = {{0}};

/* getnameinfo: reverse DNS lookup, used only for debug logging in miniwget.
 * Return non-zero (failure) so callers fall back to numeric address. */
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen,
                int flags)
{
    (void)sa; (void)salen;
    (void)host; (void)hostlen;
    (void)serv; (void)servlen;
    (void)flags;
    return 1;
}

/* herror: prints host-lookup error message, used in connecthostport
 * when NO_GETADDRINFO is defined and gethostbyname fails. */
void herror(const char *s)
{
    (void)s;
}

/* gethostbyname: legacy DNS lookup, used in connecthostport when
 * NO_GETADDRINFO / USE_GETHOSTBYNAME is defined.  VitaSDK provides no
 * implementation; returning NULL causes the caller to invoke herror() and
 * return INVALID_SOCKET — a safe failure for an unreachable code path. */
struct hostent *gethostbyname(const char *name)
{
    (void)name;
    return NULL;
}
