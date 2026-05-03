#ifdef __PSVITA__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <psp2/net/net.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

#include "context.h"
#include "vita_dns.h"

#define VITA_DNS_TIMEOUT_US (5 * 1000 * 1000)
#define VITA_DNS_RETRY 3

static bool vita_dns_resolve_inaddr(const char *hostname, SceNetInAddr *out) {
  int rid = sceNetResolverCreate("vita_dns", NULL, 0);
  if (rid < 0) {
    LOGE("vita_dns: resolver create failed rc=0x%x host=%s", rid, hostname);
    return false;
  }
  int rc = sceNetResolverStartNtoa(rid, hostname, out, VITA_DNS_TIMEOUT_US, VITA_DNS_RETRY, 0);
  sceNetResolverDestroy(rid);
  if (rc < 0) {
    LOGE("vita_dns: resolve failed host=%s rc=0x%x", hostname, rc);
    return false;
  }
  return true;
}

bool vita_curl_add_resolve(const char *hostname, int port, struct curl_slist **list) {
  if (!hostname || !list)
    return false;

  SceNetInAddr addr;
  if (!vita_dns_resolve_inaddr(hostname, &addr))
    return false;

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

bool vita_resolve_sin(const char *hostname, uint16_t port, struct sockaddr_in *out) {
  if (!hostname || !out)
    return false;

  SceNetInAddr addr;
  if (!vita_dns_resolve_inaddr(hostname, &addr))
    return false;

  char ip_str[INET_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str)))
    snprintf(ip_str, sizeof(ip_str), "<inet_ntop failed>");
  LOGD("vita_dns: resolved host=%s -> %s (sockaddr)", hostname, ip_str);

  memset(out, 0, sizeof(*out));
  out->sin_family = AF_INET;
  out->sin_port = htons(port);
  memcpy(&out->sin_addr, &addr, sizeof(addr));
  return true;
}

#endif /* __PSVITA__ */
