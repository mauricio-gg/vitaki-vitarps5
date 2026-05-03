#ifndef VITA_DNS_H
#define VITA_DNS_H
#ifdef __PSVITA__
#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>
#include <curl/curl.h>

/* Resolve hostname using sceNetResolverStartNtoa (Sony's native resolver).
 * On success, appends "hostname:port:dotted_ip" to *list and returns true.
 * *list may be NULL on entry.  Caller must pass the list to CURLOPT_RESOLVE
 * before curl_easy_perform, then free it with curl_slist_free_all() after. */
bool vita_curl_add_resolve(const char *hostname, int port, struct curl_slist **list);

/* Resolve hostname to AF_INET sockaddr_in via Sony's resolver.
 * Fills out->sin_family, out->sin_port (network byte order), and out->sin_addr.
 * Returns true on success. */
bool vita_resolve_sin(const char *hostname, uint16_t port, struct sockaddr_in *out);

#endif /* __PSVITA__ */
#endif /* VITA_DNS_H */
