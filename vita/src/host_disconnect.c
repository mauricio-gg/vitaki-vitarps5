#include "context.h"
#include "host_disconnect.h"

#include <psp2/kernel/processmgr.h>

#define HINT_DURATION_LINK_WAIT_US (3 * 1000 * 1000ULL)
#define DISCONNECT_BANNER_DEFAULT_US HINT_DURATION_LINK_WAIT_US

const char *host_quit_reason_label(ChiakiQuitReason reason) {
  switch (reason) {
    case CHIAKI_QUIT_REASON_NONE: return "No quit";
    case CHIAKI_QUIT_REASON_STOPPED: return "User stopped";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN: return "Session request failed";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED: return "Connection refused";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE: return "Remote Play already in use";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH: return "Remote Play crashed";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH: return "Remote Play version mismatch";
    case CHIAKI_QUIT_REASON_CTRL_UNKNOWN: return "Control channel failure";
    case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED: return "Control connection failed";
    case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED: return "Control connection refused";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN: return "Stream connection failure";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED: return "Console disconnected";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN: return "Console shutdown";
    case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED: return "PSN registration failed";
    default:
      return "Unspecified";
  }
}

bool host_quit_reason_requires_retry(ChiakiQuitReason reason) {
  switch (reason) {
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
      return false;
    default:
      return true;
  }
}

void host_update_disconnect_banner(const char *reason) {
  if (!reason || !reason[0])
    return;

  sceClibSnprintf(context.stream.disconnect_reason,
                  sizeof(context.stream.disconnect_reason),
                  "%s",
                  reason);
  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint64_t until = context.stream.next_stream_allowed_us;
  if (!until)
    until = now_us + DISCONNECT_BANNER_DEFAULT_US;
  context.stream.disconnect_banner_until_us = until;
}
