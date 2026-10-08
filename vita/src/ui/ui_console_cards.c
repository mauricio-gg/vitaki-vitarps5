/**
 * @file ui_console_cards.c
 * @brief Console card rendering and state management
 *
 * Data source for the Home console list: host mapping, the cached/sorted/filtered
 * console list, the selected console, and the console filter. Drawing lives in
 * ui_home.c / ui_xmb_list.c.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>

#include "ui/ui_internal.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_console_rows.h"
#include "ui/ui_console_status.h"
#include "ui/ui_text.h"
#include "ui/ui_focus.h"
#include "context.h"
#include "host.h"
#include "psn_auth.h"

// ============================================================================
// Local State
// ============================================================================

/** Currently selected console card index */
static int selected_console_index = 0;

/** Console card cache to prevent flickering during discovery updates */
static ConsoleCardCache card_cache = {0};

/** Consoles known before the filter is applied (the "M" of "N found of M"). */
static int total_console_count = 0;

/** Set by ui_cards_mark_dirty() (called from host_storage.c/discovery.c/psn_remote.c on host
 * removal) to force the next ui_cards_update_cache() call to bypass the throttle. Writers just
 * do a plain `cards_dirty = true;` (safe: a single-word store is atomic on Vita's Cortex-A9,
 * matching the project's existing volatile-flag pattern, e.g. frame_ready_for_display in
 * video.c); the single reader in ui_cards_update_cache() consumes it via
 * __atomic_exchange_n(..., __ATOMIC_SEQ_CST) so its read-and-clear can't drop a concurrent
 * mark_dirty() call. `volatile` is kept alongside the atomic builtin -- redundant here since
 * the exchange already forces a real memory access, but harmless and keeps this flag visually
 * consistent with the rest of the file's volatile cross-thread flags. */
static volatile bool cards_dirty = false;

// ============================================================================
// Filter State
// ============================================================================

#define FILTER_MAX_LEN (UI_FILTER_TEXT_MAX - 1)
static char filter_text[FILTER_MAX_LEN + 1] = {0};
static int filter_len = 0;
static bool filter_active = false;

/** IME dialog state */
static bool ime_running = false;
static SceWChar16 ime_input_buf[FILTER_MAX_LEN + 1];
static SceWChar16 ime_initial_text[FILTER_MAX_LEN + 1];
static char ime_title_buf[64];

// ============================================================================
// Filter Helpers
// ============================================================================

/**
 * utf16_to_utf8() - Convert UTF-16 to UTF-8
 * @src: Source UTF-16 string (SceWChar16)
 * @src_max: Maximum number of UTF-16 characters to read from source
 * @dst: Destination UTF-8 buffer
 * @dst_size: Size of destination buffer
 *
 * Simple converter for IME dialog output. Handles BMP (Basic Multilingual Plane)
 * characters only, which covers most common use cases on Vita.
 */
static void utf16_to_utf8(const SceWChar16 *src, size_t src_max, char *dst, size_t dst_size) {
  size_t i = 0;
  size_t o = 0;
  while (i < src_max && src[i] && o < dst_size - 1) {
    if (src[i] < 0x80) {
      dst[o++] = (char)src[i];
    } else if (src[i] < 0x800) {
      if (o + 1 >= dst_size - 1)
        break;
      dst[o++] = (char)(0xC0 | (src[i] >> 6));
      dst[o++] = (char)(0x80 | (src[i] & 0x3F));
    } else {
      if (o + 2 >= dst_size - 1)
        break;
      dst[o++] = (char)(0xE0 | (src[i] >> 12));
      dst[o++] = (char)(0x80 | ((src[i] >> 6) & 0x3F));
      dst[o++] = (char)(0x80 | (src[i] & 0x3F));
    }
    i++;
  }
  dst[o] = '\0';
}

/**
 * utf8_to_utf16() - Convert UTF-8 to UTF-16 (BMP only, the inverse of utf16_to_utf8)
 * @src: Source UTF-8 string
 * @dst: Destination buffer, always NUL-terminated
 * @dst_len: Capacity of @dst in SceWChar16 units
 *
 * Used to prefill the IME with the current filter. A character outside the BMP ends the text.
 */
static void utf8_to_utf16(const char *src, SceWChar16 *dst, size_t dst_len) {
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)src;
  while (*p && o + 1 < dst_len) {
    if (*p < 0x80) {
      dst[o++] = *p++;
    } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      dst[o++] = (SceWChar16)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
      p += 2;
    } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      dst[o++] = (SceWChar16)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
      p += 3;
    } else {
      break;
    }
  }
  dst[o] = 0;
}

// ============================================================================
// Initialization
// ============================================================================

void ui_cards_init(void) {
  selected_console_index = 0;
  memset(&card_cache, 0, sizeof(card_cache));
  total_console_count = 0;
  cards_dirty = false;
  /* Reset filter state */
  filter_text[0] = '\0';
  filter_len = 0;
  filter_active = false;
  ime_running = false;
}

// ============================================================================
// Host Mapping
// ============================================================================

UiConsoleState ui_cards_classify(const ConsoleCardInfo *card, bool token_ok, bool cooldown) {
  return ui_console_classify(card->is_registered, card->is_discovered,
                             card->state == CONSOLE_CARD_STATE_STANDBY,
                             card->has_internet && token_ok, cooldown);
}

void ui_cards_map_host(VitaChiakiHost *host, ConsoleCardInfo *card) {
  if (!host || !card)
    return;

  bool discovered = (host->type & DISCOVERED) && (host->discovery_state);
  bool registered = host->type & REGISTERED;
  bool psn_remote = host->source == VITA_HOST_SOURCE_PSN_REMOTE;
  bool at_rest =
      discovered && host->discovery_state_snapshot == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

  /* Read only the inline snapshot fields the discovery thread snprintf's in place --
   * never discovery_state->host_name/host_addr or registered_state->server_nickname,
   * which are upstream heap structs another thread can free/re-strdup concurrently. */
  if (host->display_name[0] || host->hostname[0]) {
    snprintf(card->name, sizeof(card->name), "%s",
             host->display_name[0] ? host->display_name : host->hostname);
    snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->hostname);
  } else {
    snprintf(card->name, sizeof(card->name), "%s", "Unknown");
    card->ip_address[0] = '\0';
  }

  // Map host state to console state
  if (psn_remote && registered) {
    card->status = 0;  // Available
    card->state = CONSOLE_CARD_STATE_READY;
  } else if (psn_remote) {
    card->status = 1;  // Unavailable (not registered)
    card->state = CONSOLE_CARD_STATE_UNKNOWN;
  } else if (discovered && !at_rest) {
    card->status = 0;  // Available
    card->state = CONSOLE_CARD_STATE_READY;
  } else if (at_rest) {
    card->status = 2;  // Connecting/Standby
    card->state = CONSOLE_CARD_STATE_STANDBY;
  } else {
    card->status = 1;  // Unavailable
    card->state = CONSOLE_CARD_STATE_UNKNOWN;
  }

  card->is_registered = registered;
  card->is_discovered = discovered;
  card->host = host;
  card->has_internet = host->psn_remote_available;
}

// ============================================================================
// Cache Management
// ============================================================================

void ui_cards_mark_dirty(void) {
  cards_dirty = true;
}

void ui_cards_update_cache(bool force_update) {
  uint64_t current_time = sceKernelGetProcessTimeWide();

  /* Atomically consume-and-clear the dirty flag now, before the rebuild loop below reads
   * context.hosts[] -- not after the rebuild completes. A plain read-then-clear ("bool
   * was_dirty = cards_dirty; cards_dirty = false;") has a window between the two statements:
   * a ui_cards_mark_dirty() call from another thread landing in that window would be silently
   * clobbered by the unconditional `= false` write, losing the removal until the next 10s
   * throttle window. __atomic_exchange_n reads and clears in one indivisible step, so any
   * ui_cards_mark_dirty() call is either captured in was_dirty here, or happens strictly after
   * the exchange and leaves cards_dirty true for the next ui_cards_update_cache() call to pick
   * up -- no mutation is ever lost. */
  bool was_dirty = __atomic_exchange_n(&cards_dirty, false, __ATOMIC_SEQ_CST);

  // Only update cache if enough time has passed, or if forced/dirty
  if (!force_update && !was_dirty &&
      (current_time - card_cache.last_update_time) < CARD_CACHE_UPDATE_INTERVAL_US) {
    return;
  }

  /* Count current valid hosts and apply filter */
  int num_hosts = 0;
  int num_known = 0;
  ConsoleCardInfo temp_cards[MAX_CONTEXT_HOSTS];

  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    if (context.hosts[i]) {
      ConsoleCardInfo temp = {0};
      ui_cards_map_host(context.hosts[i], &temp);
      /* Skip unregistered hosts if "show only paired" is enabled */
      if (context.config.show_only_paired && !temp.is_registered)
        continue;
      num_known++;
      /* Apply filter if active */
      if (filter_active && filter_len > 0) {
        if (!ui_console_matches_filter(temp.name, temp.ip_address, filter_text))
          continue;
      }
      temp_cards[num_hosts] = temp;
      num_hosts++;
    }
  }

  /* Stable insertion sort: paired consoles first, then by name (ui_console_order_before). */
  for (int i = 1; i < num_hosts; i++) {
    ConsoleCardInfo key = temp_cards[i];
    int j = i - 1;
    while (j >= 0 && ui_console_order_before(key.is_registered, key.name,
                                             temp_cards[j].is_registered, temp_cards[j].name)) {
      temp_cards[j + 1] = temp_cards[j];
      j--;
    }
    temp_cards[j + 1] = key;
  }

  /* Update cache — allow 0 results when filter is active (to show "no matches"), or when
   * this rebuild was explicitly requested (force_update) or triggered by a dirty-flagged
   * host removal (was_dirty). Without those two, a removal that empties the list would hit
   * neither condition below, so the write (and last_update_time) would be skipped and the
   * stale, now-empty-list-worthy cache would keep re-triggering a rebuild every frame
   * instead of converging. Bare `num_hosts > 0` stays as the unforced throttle path so
   * ordinary polling doesn't flicker the grid to empty on a transient zero-host read. */
  if (num_hosts > 0 || filter_active || force_update || was_dirty) {
    card_cache.num_cards = num_hosts;
    total_console_count = num_known;
    if (num_hosts > 0)
      memcpy(card_cache.cards, temp_cards, sizeof(ConsoleCardInfo) * num_hosts);
    card_cache.last_update_time = current_time;

    /* Clamp selection to valid range */
    if (card_cache.num_cards == 0) {
      selected_console_index = 0;
    } else if (selected_console_index >= card_cache.num_cards) {
      selected_console_index = card_cache.num_cards - 1;
    }
  }
}

// ============================================================================
// Filter IME Dialog
// ============================================================================

/**
 * ui_cards_clear_filter() - Drop the filter text and show every console again.
 */
void ui_cards_clear_filter(void) {
  filter_text[0] = '\0';
  filter_len = 0;
  filter_active = false;
  ui_cards_update_cache(true);
}

/**
 * ui_cards_edit_filter() - Open the system keyboard with the current filter text prefilled.
 *
 * Done with empty text clears the filter; Cancel leaves it as it was (see the poll below).
 */
void ui_cards_edit_filter(void) {
  if (ime_running)
    return;

  memset(ime_input_buf, 0, sizeof(ime_input_buf));
  utf8_to_utf16(filter_text, ime_initial_text,
                sizeof(ime_initial_text) / sizeof(ime_initial_text[0]));
  sceClibSnprintf(ime_title_buf, sizeof(ime_title_buf), "Filter Consoles");

  /* Convert title to UTF-16 for IME */
  SceWChar16 ime_title_w[64];
  utf8_to_utf16(ime_title_buf, ime_title_w, sizeof(ime_title_w) / sizeof(ime_title_w[0]));

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);
  param.supportedLanguages = 0; /* All languages */
  param.languagesForced = SCE_FALSE;
  param.type = SCE_IME_TYPE_DEFAULT;
  param.option = 0;
  param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
  param.maxTextLength = FILTER_MAX_LEN;
  param.title = ime_title_w;
  param.initialText = ime_initial_text;
  param.inputTextBuffer = ime_input_buf;

  int ret = sceImeDialogInit(&param);
  if (ret >= 0) {
    ime_running = true;
  } else {
    LOGE("Filter keyboard failed to open: 0x%08x", ret);
  }
}

/**
 * ui_cards_open_filter() - Start shortcut: clear an active filter, otherwise open the keyboard.
 */
void ui_cards_open_filter(void) {
  if (ime_running)
    return;
  if (filter_active)
    ui_cards_clear_filter();
  else
    ui_cards_edit_filter();
}

/**
 * ui_cards_poll_filter_ime() - Poll IME dialog state
 *
 * Call each frame to check for user input completion.
 * Handles Enter (confirm) and Cancel/Close (discard) actions.
 */
void ui_cards_poll_filter_ime(void) {
  if (!ime_running)
    return;

  SceCommonDialogStatus status = sceImeDialogGetStatus();
  if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);

    /* Done applies the typed text (empty clears the filter); Cancel keeps the filter as it was. */
    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
      utf16_to_utf8(ime_input_buf, FILTER_MAX_LEN + 1, filter_text, sizeof(filter_text));
      filter_len = (int)strlen(filter_text);
      filter_active = (filter_len > 0);
    }

    sceImeDialogTerm();
    ime_running = false;

    /* Force cache refresh to apply filter */
    ui_cards_update_cache(true);
    /* Clamp selection */
    if (selected_console_index >= card_cache.num_cards && card_cache.num_cards > 0) {
      selected_console_index = card_cache.num_cards - 1;
    }
  }
}

/**
 * ui_cards_is_filter_active() - Check if filter is active
 *
 * Returns: true if console filter is currently applied
 */
bool ui_cards_is_filter_active(void) {
  return filter_active;
}

// ============================================================================
// Selection & State Accessors
// ============================================================================

int ui_cards_get_selected_index(void) {
  return selected_console_index;
}

void ui_cards_set_selected_index(int index) {
  if (card_cache.num_cards == 0) {
    selected_console_index = 0;
    return;
  }
  if (index < 0)
    index = 0;
  if (index >= card_cache.num_cards)
    index = card_cache.num_cards - 1;
  selected_console_index = index;
}

int ui_cards_get_count(void) {
  return card_cache.num_cards;
}

ConsoleCardInfo *ui_cards_get_card(int index) {
  if (index < 0 || index >= card_cache.num_cards)
    return NULL;
  return &card_cache.cards[index];
}

ConsoleCardInfo *ui_cards_get_selected_card(void) {
  if (card_cache.num_cards == 0)
    return NULL;
  if (selected_console_index < 0 || selected_console_index >= card_cache.num_cards)
    return NULL;
  return &card_cache.cards[selected_console_index];
}

int ui_cards_get_total_count(void) {
  return total_console_count;
}

const char *ui_cards_get_filter_text(void) {
  return filter_text;
}
