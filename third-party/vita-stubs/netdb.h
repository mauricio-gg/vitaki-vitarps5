/*
 * vita-stubs/netdb.h — stub for gai_strerror on VitaSDK
 *
 * VitaSDK has no <netdb.h>.  miniupnpc calls gai_strerror() when
 * NO_GETADDRINFO is NOT set, but older build systems worked around
 * this with -Dgai_strerror=strerror.  That macro trick broke under
 * GCC 15 because the textual replacement creates a conflicting
 * redeclaration:
 *
 *   const char *strerror(int)   ← what POSIX gai_strerror returns
 *   char       *strerror(int)   ← what VitaSDK string.h declares
 *
 * This inline wrapper satisfies the POSIX const char * return type
 * while delegating to the SDK's strerror() via an explicit cast,
 * with no conflicting declarations.
 */
#ifndef _VITA_STUBS_NETDB_H
#define _VITA_STUBS_NETDB_H

#include <string.h>

static inline const char *gai_strerror(int ecode)
{
    return (const char *)strerror(ecode);
}

#endif /* _VITA_STUBS_NETDB_H */
