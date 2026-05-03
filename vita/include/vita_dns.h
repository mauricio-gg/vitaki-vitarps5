#ifndef VITA_DNS_H
#define VITA_DNS_H
#ifdef __PSVITA__
#include <stdbool.h>
#include <curl/curl.h>

/* Resolve hostname using sceNetResolverStartNtoa (Sony's native resolver).
 * On success, appends "hostname:port:dotted_ip" to *list and returns true.
 * *list may be NULL on entry.  Caller must pass the list to CURLOPT_RESOLVE
 * before curl_easy_perform, then free it with curl_slist_free_all() after. */
bool vita_curl_add_resolve(const char *hostname, int port, struct curl_slist **list);

#endif /* __PSVITA__ */
#endif /* VITA_DNS_H */
