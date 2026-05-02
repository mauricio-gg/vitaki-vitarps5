/*
 * vita-stubs/netdb.h — stub for missing POSIX network-name symbols on VitaSDK
 *
 * VitaSDK has no <netdb.h>. The following symbols are missing:
 *
 * 1. gai_strerror(): returns const char * — the -Dgai_strerror=strerror
 *    macro trick broke under GCC 15 (conflicting const vs non-const return
 *    type redeclaration of strerror). Replaced by this inline wrapper.
 *
 * 2. getnameinfo() + NI_* flags: used by miniwget.c for debug logging.
 *    The function is defined as a no-op in posix_stubs.c; the declaration
 *    and flag constants are provided here so GCC 15 (which hard-errors on
 *    implicit declarations) can compile the call sites.
 *
 * 3. struct hostent + gethostbyname() + herror(): used by connecthostport.c
 *    when NO_GETADDRINFO is defined (USE_GETHOSTBYNAME path). The struct and
 *    declarations are provided here; stubs are in posix_stubs.c.
 */
#ifndef _VITA_STUBS_NETDB_H
#define _VITA_STUBS_NETDB_H

#include <string.h>
#include <sys/socket.h>

/* gai_strerror: map error code to string (used by miniupnpc.c) */
static inline const char *gai_strerror(int ecode)
{
    return (const char *)strerror(ecode);
}

/* ---- getnameinfo ---- */

/* NI_* flags for getnameinfo() — standard POSIX values */
#define NI_NUMERICHOST  1
#define NI_NUMERICSERV  2
#define NI_NOFQDN       4
#define NI_NAMEREQD     8
#define NI_DGRAM        16

/* getnameinfo: defined as a no-op in posix_stubs.c */
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen,
                int flags);

/* ---- gethostbyname / struct hostent ---- */

/*
 * struct hostent: minimal definition sufficient for the USE_GETHOSTBYNAME
 * code path in connecthostport.c, which only accesses hp->h_addr.
 * h_addr is the traditional POSIX alias for h_addr_list[0].
 */
struct hostent {
    char  *h_name;        /* official name of host */
    char **h_aliases;     /* alias list */
    int    h_addrtype;    /* host address type */
    int    h_length;      /* length of address */
    char **h_addr_list;   /* list of addresses from name server */
};
#define h_addr h_addr_list[0]

/* gethostbyname: defined as a returning-NULL stub in posix_stubs.c */
struct hostent *gethostbyname(const char *name);

/* herror: defined as a no-op stub in posix_stubs.c */
void herror(const char *s);

#endif /* _VITA_STUBS_NETDB_H */
