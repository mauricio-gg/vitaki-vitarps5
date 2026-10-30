// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mic_decide.h"

MicDecision mic_decide(const MicDecideIn *in) {
  MicDecision d = {.action = MIC_ACTION_NONE};
  const bool want_sending = !in->muted && !in->stop_requested;
  if (want_sending && !in->sending) {
    d.action = MIC_ACTION_OPEN;
    d.connect_first = !in->connected;
  } else if (!want_sending && in->sending) {
    d.action = MIC_ACTION_CLOSE;
    d.announce = !in->stop_requested;
  }
  return d;
}
