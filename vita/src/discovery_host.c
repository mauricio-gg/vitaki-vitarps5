#include <stdlib.h>
#include <string.h>

#include "discovery.h"

static char *duplicate_string(const char *src) {
  if (!src)
    return NULL;
  return strdup(src);
}

void destroy_discovery_host(ChiakiDiscoveryHost *host) {
  if (!host)
    return;
#define FREE_FIELD(name)           \
  do {                             \
    if (host->name) {              \
      free((void *)host->name);    \
      host->name = NULL;           \
    }                              \
  } while (0)
  CHIAKI_DISCOVERY_HOST_STRING_FOREACH(FREE_FIELD)
#undef FREE_FIELD
  free(host);
}

ChiakiDiscoveryHost *copy_discovery_host(const ChiakiDiscoveryHost *src) {
  if (!src)
    return NULL;

  ChiakiDiscoveryHost *dest = (ChiakiDiscoveryHost *)malloc(sizeof(ChiakiDiscoveryHost));
  if (!dest)
    return NULL;
  memcpy(dest, src, sizeof(ChiakiDiscoveryHost));

#define RESET_FIELD(name) dest->name = NULL;
  CHIAKI_DISCOVERY_HOST_STRING_FOREACH(RESET_FIELD)
#undef RESET_FIELD

#define DUP_FIELD(name)                                         \
  do {                                                          \
    dest->name = duplicate_string(src->name);                   \
    if (src->name && !dest->name) {                             \
      destroy_discovery_host(dest);                             \
      return NULL;                                              \
    }                                                           \
  } while (0)
  CHIAKI_DISCOVERY_HOST_STRING_FOREACH(DUP_FIELD)
#undef DUP_FIELD

  return dest;
}
