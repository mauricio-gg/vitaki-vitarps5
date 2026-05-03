#ifdef __PSVITA__
#include <arpa/inet.h>
#include <psp2/net/net.h>
#include <stdio.h>

#include <curl/curl.h>

#include "context.h"
#include "vita_dns.h"

#define VITA_DNS_TIMEOUT_MS 5000
#define VITA_DNS_RETRY 3

bool vita_curl_add_resolve(const char *hostname, int port, struct curl_slist **list) {
  if (!hostname || !list)
    return false;

  SceNetInAddr addr;
  int rid = sceNetResolverCreate("vita_dns", NULL, 0);
  if (rid < 0) {
    LOGE("vita_dns: resolver create failed rc=0x%x host=%s", rid, hostname);
    return false;
  }

  int rc = sceNetResolverStartNtoa(rid, hostname, &addr, VITA_DNS_TIMEOUT_MS, VITA_DNS_RETRY, 0);
  sceNetResolverDestroy(rid);
  if (rc < 0) {
    LOGE("vita_dns: resolve failed host=%s rc=0x%x", hostname, rc);
    return false;
  }

  char ip_str[INET_ADDRSTRLEN];
  if (!inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str)))
    return false;

  LOGD("vita_dns: resolved host=%s -> %s", hostname, ip_str);

  char entry[320];
  snprintf(entry, sizeof(entry), "%s:%d:%s", hostname, port, ip_str);
  struct curl_slist *tmp = curl_slist_append(*list, entry);
  if (!tmp)
    return false;
  *list = tmp;
  return true;
}

#endif /* __PSVITA__ */
