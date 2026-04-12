/*
 * Vita stub for <net/if.h>
 * Provides IFNAMSIZ, struct ifreq, if_nametoindex(), and IPv6 socket-option
 * constants needed by miniupnpc (minissdpc.c).
 *
 * VitaSDK's <netinet/in.h> does not define IPPROTO_IPV6 or the IPV6_*
 * multicast socket options.  These constants are only referenced inside IPv6
 * code paths that are never reached on Vita: upnpDiscover() is always called
 * with ipv6=0, so the setsockopt() calls that use them are dead code.
 * The defines below exist solely to satisfy the compiler.
 */
#ifndef VITA_STUB_NET_IF_H
#define VITA_STUB_NET_IF_H

#include <sys/socket.h>

#define IFNAMSIZ 16

/* IPv6 protocol / socket-option stubs (dead code paths on Vita) */
#include <netinet/in.h>

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6        41
#endif

#ifndef IPV6_MULTICAST_IF
#define IPV6_MULTICAST_IF   17
#endif

#ifndef IPV6_MULTICAST_HOPS
#define IPV6_MULTICAST_HOPS 18
#endif

extern const struct in6_addr in6addr_any;

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifr_addr;
        char            ifr_data[16];
    };
};

static inline unsigned int if_nametoindex(const char *ifname)
{
    (void)ifname;
    return 0;
}

#endif /* VITA_STUB_NET_IF_H */
