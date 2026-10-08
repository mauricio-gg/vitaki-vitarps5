/**
 * @file ui_reconnecting.c
 * @brief The Reconnecting screen, "Optimizing Stream" (SPEC.md section 3.4)
 */

#include "ui/ui_reconnecting.h"

#include <stdint.h>
#include <stdio.h>

#include "context.h"
#include "ui/ui_connecting_ring.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_spinner.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"
#include "ui/ui_top_bar.h"

#define ART_CX (UI_CONN_ART_X + UI_CONN_ART_W / 2)
#define ART_CY (UI_CONN_ART_Y + UI_CONN_ART_H / 2)
#define NOTE_Y (UI_RECON_Y + UI_RECON_LINE_H + UI_RECON_BITRATE_H + UI_RECON_NOTE_GAP)
#define KBPS_PER_MBPS 1000.0f
#define TEXT_MAX 32

static const char TITLE[] = "Optimizing Stream";
static const char RECOVERING[] = "Recovering from packet loss";
static const char PLEASE_WAIT[] = "Please wait...";

/* Text built from the stream state, rebuilt only when the numbers change. */
static char s_bitrate_text[TEXT_MAX];
static char s_attempt_text[TEXT_MAX];
static uint32_t s_bitrate_kbps = UINT32_MAX;
static uint32_t s_attempts = UINT32_MAX;

/** Rebuild the bitrate and attempt lines when their numbers changed. */
static void update_text(void) {
  const uint32_t kbps = context.stream.recovery_bitrate_kbps;
  if (kbps != s_bitrate_kbps) {
    s_bitrate_kbps = kbps;
    const uint32_t shown = kbps > 0 ? kbps : UI_RECON_DEFAULT_KBPS;
    snprintf(s_bitrate_text, sizeof(s_bitrate_text), "Retrying at %.2f Mbps",
             (double)((float)shown / KBPS_PER_MBPS));
  }
  const uint32_t attempts = context.stream.loss_retry_attempts;
  if (attempts != s_attempts) {
    s_attempts = attempts;
    snprintf(s_attempt_text, sizeof(s_attempt_text), "Attempt %u", (unsigned)attempts);
  }
}

void ui_reconnecting_frame(void) {
  update_text();

  ui_page_frame_draw(UI_PAGE_ICON_WIFI, TITLE);
  ui_top_bar_draw(NULL);
  ui_draw_halo(ART_CX, ART_CY);
  ui_spinner_draw(UI_SPINNER_LARGE, ART_CX, ART_CY);

  ui_text_draw_face_centered_v(UI_FACE_T20, UI_RECON_X, UI_RECON_Y, UI_RECON_LINE_H, UI_TEXT_2,
                               RECOVERING);
  ui_text_draw_face_centered_v(UI_FACE_T28, UI_RECON_X, UI_RECON_Y + UI_RECON_LINE_H,
                               UI_RECON_BITRATE_H, UI_TEXT, s_bitrate_text);
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_RECON_X, NOTE_Y, UI_T16_LINE, UI_TEXT_3,
                               s_attempt_text);
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_RECON_X, NOTE_Y + UI_T16_LINE, UI_T16_LINE,
                               UI_TEXT_3, PLEASE_WAIT);
}
