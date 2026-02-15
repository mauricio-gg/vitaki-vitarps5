/**
 * @file ui_console_cards.c
 * @brief Console card rendering and state management
 *
 * Implements the console card grid display for the main UI screen.
 * Handles card rendering, animations, caching, and host mapping.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>

#include "ui/ui_internal.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_focus.h"
#include "context.h"
#include "host.h"

// ============================================================================
// Local State
// ============================================================================

/** Currently selected console card index */
static int selected_console_index = 0;

/** Console card cache to prevent flickering during discovery updates */
static ConsoleCardCache card_cache = {0};

/** Card focus animation state */
static CardFocusAnimState card_focus_anim = {
    .focused_card_index = -1,
    .current_scale = CONSOLE_CARD_FOCUS_SCALE_MIN,
    .focus_start_us = 0,
    .previous_focused_card_index = -1,
    .unfocus_start_us = 0
};

// ============================================================================
// Horizontal Scroll State
// ============================================================================

/** Index of leftmost visible card in the carousel */
static int scroll_offset = 0;

/** Scroll animation state */
static float scroll_anim_progress = 1.0f;  // 0→1, starts complete
static int scroll_anim_from = 0;           // Scroll offset at animation start
static int scroll_anim_to = 0;             // Target scroll offset
static uint64_t scroll_anim_start_us = 0;  // Animation start timestamp

// ============================================================================
// Filter State
// ============================================================================

#define FILTER_MAX_LEN  31
static char filter_text[FILTER_MAX_LEN + 1] = {0};
static int filter_len = 0;
static bool filter_active = false;

/** IME dialog state */
static bool ime_running = false;
static SceWChar16 ime_input_buf[FILTER_MAX_LEN + 1];
static SceWChar16 ime_initial_text[FILTER_MAX_LEN + 1];
static char ime_title_buf[64];

// ============================================================================
// Forward Declarations
// ============================================================================

static void update_card_focus_animation(int new_focus_index);
static float get_card_scale(int card_index, bool is_focused);

// ============================================================================
// Filter Helpers
// ============================================================================

/**
 * str_contains_nocase() - Case-insensitive substring search
 * @haystack: String to search in
 * @needle: String to search for
 *
 * Returns: true if needle is found in haystack (case-insensitive ASCII)
 */
static bool str_contains_nocase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return true;
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);
    if (needle_len > haystack_len) return false;
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        bool match = true;
        for (size_t j = 0; j < needle_len; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            /* Simple ASCII case-insensitive */
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

/**
 * utf16_to_utf8() - Convert UTF-16 to UTF-8
 * @src: Source UTF-16 string (SceWChar16)
 * @dst: Destination UTF-8 buffer
 * @dst_size: Size of destination buffer
 *
 * Simple converter for IME dialog output. Handles BMP (Basic Multilingual Plane)
 * characters only, which covers most common use cases on Vita.
 */
static void utf16_to_utf8(const SceWChar16* src, char* dst, size_t dst_size) {
    size_t i = 0;
    size_t o = 0;
    while (src[i] && o < dst_size - 1) {
        if (src[i] < 0x80) {
            dst[o++] = (char)src[i];
        } else if (src[i] < 0x800) {
            if (o + 1 >= dst_size - 1) break;
            dst[o++] = (char)(0xC0 | (src[i] >> 6));
            dst[o++] = (char)(0x80 | (src[i] & 0x3F));
        } else {
            if (o + 2 >= dst_size - 1) break;
            dst[o++] = (char)(0xE0 | (src[i] >> 12));
            dst[o++] = (char)(0x80 | ((src[i] >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (src[i] & 0x3F));
        }
        i++;
    }
    dst[o] = '\0';
}

// ============================================================================
// Initialization
// ============================================================================

void ui_cards_init(void) {
    selected_console_index = 0;
    memset(&card_cache, 0, sizeof(card_cache));
    card_focus_anim.focused_card_index = -1;
    card_focus_anim.current_scale = CONSOLE_CARD_FOCUS_SCALE_MIN;
    card_focus_anim.focus_start_us = 0;
    card_focus_anim.previous_focused_card_index = -1;
    card_focus_anim.unfocus_start_us = 0;
    /* Reset scroll state */
    scroll_offset = 0;
    scroll_anim_progress = 1.0f;
    scroll_anim_from = 0;
    scroll_anim_to = 0;
    scroll_anim_start_us = 0;
    /* Reset filter state */
    filter_text[0] = '\0';
    filter_len = 0;
    filter_active = false;
    ime_running = false;
}

// ============================================================================
// Host Mapping
// ============================================================================

void ui_cards_map_host(VitaChiakiHost* host, ConsoleCardInfo* card) {
    if (!host || !card) return;

    bool discovered = (host->type & DISCOVERED) && (host->discovery_state);
    bool registered = host->type & REGISTERED;
    bool at_rest = discovered && host->discovery_state &&
                   host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

    // Copy host name
    if (discovered && host->discovery_state) {
        snprintf(card->name, sizeof(card->name), "%s", host->discovery_state->host_name);
        snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->discovery_state->host_addr);
    } else if (registered && host->registered_state) {
        snprintf(card->name, sizeof(card->name), "%s", host->registered_state->server_nickname);
        snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->hostname);
    } else if (host->hostname) {
        snprintf(card->name, sizeof(card->name), "%s", host->hostname);
        snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->hostname);
    }

    // Map host state to console state
    if (discovered && !at_rest) {
        card->status = 0;  // Available
        card->state = 1;   // Ready
    } else if (at_rest) {
        card->status = 2;  // Connecting/Standby
        card->state = 2;   // Standby
    } else {
        card->status = 1;  // Unavailable
        card->state = 0;   // Unknown
    }

    card->is_registered = registered;
    card->is_discovered = discovered;
    card->host = host;
}

// ============================================================================
// Cache Management
// ============================================================================

void ui_cards_update_cache(bool force_update) {
    uint64_t current_time = sceKernelGetProcessTimeWide();

    // Only update cache if enough time has passed or if forced
    if (!force_update &&
        (current_time - card_cache.last_update_time) < CARD_CACHE_UPDATE_INTERVAL_US) {
        return;
    }

    /* Count current valid hosts and apply filter */
    int num_hosts = 0;
    ConsoleCardInfo temp_cards[MAX_CONTEXT_HOSTS];

    for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
        if (context.hosts[i]) {
            ConsoleCardInfo temp;
            ui_cards_map_host(context.hosts[i], &temp);
            /* Apply filter if active */
            if (filter_active && filter_len > 0) {
                if (!str_contains_nocase(temp.name, filter_text))
                    continue;
            }
            temp_cards[num_hosts] = temp;
            num_hosts++;
        }
    }

    /* Update cache — allow 0 results when filter is active (to show "no matches") */
    if (num_hosts > 0 || filter_active) {
        card_cache.num_cards = num_hosts;
        if (num_hosts > 0)
            memcpy(card_cache.cards, temp_cards, sizeof(ConsoleCardInfo) * num_hosts);
        card_cache.last_update_time = current_time;

        /* Clamp selection and scroll offset to valid range */
        if (card_cache.num_cards == 0) {
            selected_console_index = 0;
        } else if (selected_console_index >= card_cache.num_cards) {
            selected_console_index = card_cache.num_cards - 1;
        }
        int max_scroll = card_cache.num_cards - CARDS_VISIBLE_MAX;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    }
}

// ============================================================================
// Animation
// ============================================================================

/**
 * update_card_focus_animation() - Update card focus animation state
 * @new_focus_index: Index of newly focused card (-1 if none)
 *
 * Call once per frame before rendering cards.
 * Handles scale-up animation for newly focused cards and scale-down
 * animation for previously focused cards.
 */
static void update_card_focus_animation(int new_focus_index) {
    uint64_t now_us = sceKernelGetProcessTimeWide();

    // Focus changed?
    if (new_focus_index != card_focus_anim.focused_card_index) {
        // Track the previous focused card for scale-down animation
        if (card_focus_anim.focused_card_index >= 0) {
            card_focus_anim.previous_focused_card_index = card_focus_anim.focused_card_index;
            card_focus_anim.unfocus_start_us = now_us;
        }

        card_focus_anim.focused_card_index = new_focus_index;
        card_focus_anim.focus_start_us = now_us;
    }

    // Calculate animation progress for currently focused card
    if (card_focus_anim.focused_card_index >= 0 && card_focus_anim.focus_start_us > 0) {
        float elapsed_ms = (float)(now_us - card_focus_anim.focus_start_us) / 1000.0f;
        float progress = elapsed_ms / (float)CONSOLE_CARD_FOCUS_DURATION_MS;

        if (progress >= 1.0f) {
            card_focus_anim.current_scale = CONSOLE_CARD_FOCUS_SCALE_MAX;
        } else {
            // Cubic ease-out for smooth feel
            float eased = 1.0f - powf(1.0f - progress, 3.0f);
            card_focus_anim.current_scale = ui_lerp(CONSOLE_CARD_FOCUS_SCALE_MIN,
                                                     CONSOLE_CARD_FOCUS_SCALE_MAX, eased);
        }
    } else {
        card_focus_anim.current_scale = CONSOLE_CARD_FOCUS_SCALE_MIN;
    }

    // Clear previous focused card index once its scale-down animation completes
    if (card_focus_anim.previous_focused_card_index >= 0 && card_focus_anim.unfocus_start_us > 0) {
        float elapsed_ms = (float)(now_us - card_focus_anim.unfocus_start_us) / 1000.0f;
        if (elapsed_ms >= (float)CONSOLE_CARD_FOCUS_DURATION_MS) {
            card_focus_anim.previous_focused_card_index = -1;
        }
    }
}

/**
 * get_card_scale() - Get scale for a specific card based on focus state
 * @card_index: Index of the card
 * @is_focused: Whether the card is currently focused
 *
 * Returns: Scale factor (0.95 to 1.0) for the card
 */
static float get_card_scale(int card_index, bool is_focused) {
    // Bounds check for invalid indices
    if (card_index < 0) {
        return CONSOLE_CARD_FOCUS_SCALE_MIN;
    }

    // Currently focused card: use scale-up animation
    if (is_focused && card_index == card_focus_anim.focused_card_index) {
        return card_focus_anim.current_scale;
    }

    // Previously focused card: animate scale-down
    if (card_index == card_focus_anim.previous_focused_card_index && card_focus_anim.unfocus_start_us > 0) {
        uint64_t now_us = sceKernelGetProcessTimeWide();
        float elapsed_ms = (float)(now_us - card_focus_anim.unfocus_start_us) / 1000.0f;
        float progress = elapsed_ms / (float)CONSOLE_CARD_FOCUS_DURATION_MS;

        if (progress >= 1.0f) {
            return CONSOLE_CARD_FOCUS_SCALE_MIN;
        } else {
            // Cubic ease-out for smooth scale-down
            float eased = 1.0f - powf(1.0f - progress, 3.0f);
            return ui_lerp(CONSOLE_CARD_FOCUS_SCALE_MAX, CONSOLE_CARD_FOCUS_SCALE_MIN, eased);
        }
    }

    return CONSOLE_CARD_FOCUS_SCALE_MIN;
}

/**
 * Start a smooth scroll animation to a target offset
 */
static void start_scroll_animation(int target_offset) {
    if (target_offset == scroll_offset && scroll_anim_progress >= 1.0f)
        return;
    scroll_anim_from = scroll_offset;
    scroll_anim_to = target_offset;
    scroll_anim_progress = 0.0f;
    scroll_anim_start_us = sceKernelGetProcessTimeWide();
}

/**
 * Update scroll animation, returns current interpolated offset as float
 */
static float update_scroll_animation(void) {
    if (scroll_anim_progress >= 1.0f)
        return (float)scroll_offset;

    uint64_t now_us = sceKernelGetProcessTimeWide();
    float elapsed_ms = (float)(now_us - scroll_anim_start_us) / 1000.0f;
    float progress = elapsed_ms / (float)CARD_SCROLL_ANIM_MS;

    if (progress >= 1.0f) {
        scroll_anim_progress = 1.0f;
        scroll_offset = scroll_anim_to;
        return (float)scroll_offset;
    }

    scroll_anim_progress = progress;
    /* Cubic ease-out */
    float eased = 1.0f - powf(1.0f - progress, 3.0f);
    return ui_lerp((float)scroll_anim_from, (float)scroll_anim_to, eased);
}

// ============================================================================
// Filter IME Dialog
// ============================================================================

/**
 * ui_cards_open_filter() - Open IME keyboard to filter consoles
 *
 * If filter is already active, clears it instead of opening IME.
 * Press Start to toggle filter on/off.
 */
void ui_cards_open_filter(void) {
    if (ime_running) return;

    /* If filter is already active, clear it instead of opening IME */
    if (filter_active) {
        filter_text[0] = '\0';
        filter_len = 0;
        filter_active = false;
        ui_cards_update_cache(true);
        ui_cards_ensure_selected_visible();
        return;
    }

    memset(ime_input_buf, 0, sizeof(ime_input_buf));
    memset(ime_initial_text, 0, sizeof(ime_initial_text));
    sceClibSnprintf(ime_title_buf, sizeof(ime_title_buf), "Filter Consoles");

    /* Convert title to UTF-16 for IME */
    SceWChar16 ime_title_w[64];
    for (int i = 0; i < 63 && ime_title_buf[i]; i++) {
        ime_title_w[i] = (SceWChar16)ime_title_buf[i];
        ime_title_w[i + 1] = 0;
    }

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0;  /* All languages */
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
    }
}

/**
 * ui_cards_poll_filter_ime() - Poll IME dialog state
 *
 * Call each frame to check for user input completion.
 * Handles Enter (confirm) and Cancel/Close (discard) actions.
 */
void ui_cards_poll_filter_ime(void) {
    if (!ime_running) return;

    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result;
        memset(&result, 0, sizeof(result));
        sceImeDialogGetResult(&result);

        if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
            /* User confirmed — convert UTF-16 to UTF-8 */
            utf16_to_utf8(ime_input_buf, filter_text, sizeof(filter_text));
            filter_len = (int)strlen(filter_text);
            filter_active = (filter_len > 0);
        }
        /* Cancel or empty = clear filter */
        if (result.button != SCE_IME_DIALOG_BUTTON_ENTER || filter_len == 0) {
            filter_text[0] = '\0';
            filter_len = 0;
            filter_active = false;
        }

        sceImeDialogTerm();
        ime_running = false;

        /* Force cache refresh to apply filter */
        ui_cards_update_cache(true);
        /* Clamp selection */
        if (selected_console_index >= card_cache.num_cards && card_cache.num_cards > 0) {
            selected_console_index = card_cache.num_cards - 1;
        }
        scroll_offset = 0;
        ui_cards_ensure_selected_visible();
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
// Rendering
// ============================================================================

void ui_cards_render_single(ConsoleCardInfo* console, int x, int y, bool selected,
                             bool cooldown_for_card, float scale) {
    if (!console) return;

    bool is_registered = console->is_registered;
    bool is_discovered = console->is_discovered;
    bool is_unpaired = is_discovered && !is_registered;
    bool is_cooldown_card = cooldown_for_card;

    // Apply scale parameter to card dimensions
    int base_w = CONSOLE_CARD_WIDTH;
    int base_h = CONSOLE_CARD_HEIGHT;
    int card_w = (int)(base_w * scale);
    int card_h = (int)(base_h * scale);

    // Center scaled card on original position
    int offset_x = (base_w - card_w) / 2;
    int offset_y = (base_h - card_h) / 2;
    int draw_x = x + offset_x;
    int draw_y = y + offset_y;

    // Status border color (awake=light blue, asleep=yellow, unpaired=grey)
    uint32_t border_color = UI_COLOR_PRIMARY_BLUE;  // Default selection blue
    if (!selected && is_unpaired) {
        border_color = RGBA8(120, 120, 120, 255);  // Grey for unpaired
    } else if (!selected && console->state == 1) {  // Ready/Awake
        border_color = RGBA8(52, 144, 255, 255);  // Light blue
    } else if (!selected && console->state == 2) {  // Standby/Asleep
        border_color = RGBA8(255, 193, 7, 255);  // Yellow
    } else if (is_cooldown_card) {
        border_color = RGBA8(0xF4, 0x43, 0x36, 255);
    }

    // Draw status border
    if (!selected || is_unpaired) {
        ui_draw_rounded_rect(draw_x - 3, draw_y - 3, card_w + 6, card_h + 6, 12, border_color);
    }

    // Enhanced selection highlight with 2px glow stroke (only for paired consoles)
    if (selected && !is_unpaired) {
        // 2px outline glow
        ui_draw_rounded_rect(draw_x - 2, draw_y - 2, card_w + 4, card_h + 4, 14, CONSOLE_CARD_GLOW_COLOR);
    }

    // Card background (greyed out for unpaired consoles, slightly lighter neutral grey when selected)
    uint32_t card_bg = is_unpaired ? RGBA8(0x25, 0x25, 0x28, 255) :
                       (selected ? RGBA8(0x38, 0x3D, 0x42, 255) : UI_COLOR_CARD_BG);
    if (is_cooldown_card)
        card_bg = RGBA8(0x1D, 0x1F, 0x24, 255);

    // Enhanced shadow for selected cards
    int shadow_offset = selected ? 6 : 4;
    uint32_t shadow_color = selected ? RGBA8(0x00, 0x00, 0x00, 80) : UI_COLOR_SHADOW;
    ui_draw_rounded_rect(draw_x + shadow_offset, draw_y + shadow_offset, card_w, card_h, 12, shadow_color);
    ui_draw_rounded_rect(draw_x, draw_y, card_w, card_h, 12, card_bg);

    // PS5 logo (centered, with max width and top padding per spec)
    bool is_ps5 = console->host && chiaki_target_is_ps5(console->host->target);

    // Calculate shared layout values for logo centering (used by both PS5 and PS4)
    int name_bar_start = card_h - (int)(CARD_NAME_BAR_BOTTOM_OFFSET * scale);
    int available_top = (int)(CARD_LOGO_TOP_PADDING * scale);
    int available_height = name_bar_start - available_top;

    if (is_ps5 && ps5_logo) {
        int logo_w = vita2d_texture_get_width(ps5_logo);
        int logo_h = vita2d_texture_get_height(ps5_logo);

        // Scale logo with max width, applying card scale
        float max_logo_w = fminf((float)(CARD_LOGO_MAX_WIDTH * scale), card_w * 0.6f);
        float logo_scale = max_logo_w / logo_w;

        int logo_scaled_w = (int)(logo_w * logo_scale);
        int logo_scaled_h = (int)(logo_h * logo_scale);
        int logo_x = draw_x + (card_w / 2) - (logo_scaled_w / 2);

        // Center logo vertically in available space above name bar
        int logo_y = draw_y + available_top + (available_height - logo_scaled_h) / 2;

        // Dimmed for unpaired consoles
        bool dim_logo = is_unpaired || is_cooldown_card;
        if (dim_logo) {
            vita2d_draw_texture_tint_scale(ps5_logo, logo_x, logo_y, logo_scale, logo_scale,
                                           RGBA8(255, 255, 255, 100));
        } else {
            vita2d_draw_texture_scale(ps5_logo, logo_x, logo_y, logo_scale, logo_scale);
        }
    } else if (!is_ps5) {
        // Fallback to PS4 icon for PS4 consoles (using same centering logic as PS5)
        vita2d_texture* logo = img_ps4;
        if (logo) {
            int logo_w = vita2d_texture_get_width(logo);
            int logo_h = vita2d_texture_get_height(logo);
            int logo_x = draw_x + (card_w / 2) - (logo_w / 2);
            // Center logo vertically in available space above name bar (consistent with PS5)
            int logo_y = draw_y + available_top + (available_height - logo_h) / 2;
            if (is_unpaired || is_cooldown_card) {
                vita2d_draw_texture_tint(logo, logo_x, logo_y,
                                         RGBA8(255, 255, 255, 120));
            } else {
                vita2d_draw_texture(logo, logo_x, logo_y);
            }
        }
    }

    // Console name bar (adjusted for 200x200 card)
    int name_bar_h = (int)(40 * scale);
    int name_bar_y = draw_y + card_h - (int)(CARD_NAME_BAR_BOTTOM_OFFSET * scale);  // Position name bar near bottom of card
    int name_bar_padding = (int)(12 * scale);
    ui_draw_rounded_rect(draw_x + name_bar_padding, name_bar_y, card_w - name_bar_padding * 2, name_bar_h, (int)(8 * scale),
        RGBA8(70, 75, 80, 255));

    // Console name text (centered in bar)
    int text_width = vita2d_font_text_width(font, CARD_TITLE_FONT_SIZE, console->name);
    int text_x = draw_x + (card_w / 2) - (text_width / 2);
    int text_y = name_bar_y + (name_bar_h / 2) + CARD_TEXT_BASELINE_OFFSET;
    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, CARD_TITLE_FONT_SIZE, console->name);

    // Status indicator (top-right)
    vita2d_texture* status_tex = NULL;
    if (is_cooldown_card) {
        status_tex = ellipse_red;
    } else {
        if (console->status == 0) status_tex = ellipse_green;
        else if (console->status == 1) status_tex = ellipse_red;
        else if (console->status == 2) status_tex = ellipse_yellow;
    }

    if (status_tex) {
        int indicator_x = draw_x + card_w - (int)(35 * scale);
        int indicator_y = draw_y + (int)(10 * scale);
        if (is_cooldown_card) {
            uint64_t now_ms = sceKernelGetProcessTimeWide() / 1000ULL;
            float phase = (float)(now_ms % 1600) / 1600.0f;
            float pulse = (sinf(phase * 2.0f * M_PI) + 1.0f) * 0.5f;
            uint8_t channel = (uint8_t)(190 + pulse * 50.0f);
            uint32_t wait_color = RGBA8(channel, channel, channel, 255);
            const char *wait_text = "Please wait...";
            int wait_w = vita2d_font_text_width(font, FONT_SIZE_BODY, wait_text);
            int text_x = draw_x + (card_w - wait_w) / 2;
            int text_y = indicator_y + FONT_SIZE_BODY;
            vita2d_font_draw_text(font, text_x, text_y,
                                  wait_color, FONT_SIZE_BODY, wait_text);
            vita2d_draw_texture_scale(status_tex, indicator_x, indicator_y, scale, scale);
        } else {
            // Batch 4: Status dot breathing animation (0.7-1.0 alpha over 1.5s cycle)
            uint64_t time_us = sceKernelGetProcessTimeWide();
            float time_sec = (float)(time_us % 1500000ULL) / 1000000.0f;  // 1.5s period
            float breath = 0.7f + 0.3f * ((sinf(time_sec * 2.0f * M_PI / 1.5f) + 1.0f) / 2.0f);
            uint8_t alpha = (uint8_t)(255.0f * breath);

            // Apply breathing alpha to status texture with scale
            uint32_t status_color = RGBA8(255, 255, 255, alpha);
            vita2d_draw_texture_tint_scale(status_tex, indicator_x, indicator_y, scale, scale, status_color);
        }
    }

    // State text ("Ready" / "Standby" / "Unpaired")
    const char* state_text = NULL;
    uint32_t state_color = UI_COLOR_TEXT_SECONDARY;

    if (is_unpaired) {
        state_text = "Unpaired";
        state_color = RGBA8(180, 180, 180, 255);  // Light grey
    } else if (console->state == 1) {
        state_text = "Ready";
        state_color = RGBA8(52, 144, 255, 255);  // PlayStation Blue
    } else if (console->state == 2) {
        state_text = "Standby";
        state_color = RGBA8(255, 193, 7, 255);  // Yellow
    }

    if (is_cooldown_card) {
        state_text = NULL;
    }

    if (state_text) {
        int state_text_width = vita2d_font_text_width(font, 18, state_text);
        int state_x = draw_x + (card_w / 2) - (state_text_width / 2);
        // Position status text 15px below name bar (name_bar_y + name_bar_h + 15px gap + text baseline)
        int state_y = name_bar_y + name_bar_h + 15 + 18;  // 15px gap + font size for baseline
        vita2d_font_draw_text(font, state_x, state_y, state_color, 18, state_text);
    }

    // Temporary status hints (e.g., Remote Play errors)
    if (console->host && console->host->status_hint[0]) {
        uint64_t now_us = sceKernelGetProcessTimeWide();
        if (console->host->status_hint_expire_us == 0 ||
            now_us <= console->host->status_hint_expire_us) {
            uint32_t hint_color = console->host->status_hint_is_error
                                      ? RGBA8(255, 128, 128, 255)
                                      : UI_COLOR_TEXT_SECONDARY;
            int hint_width = vita2d_font_text_width(font, 16, console->host->status_hint);
            int hint_x = draw_x + (card_w / 2) - (hint_width / 2);
            // Position hint text 8px below status text
            int hint_y = name_bar_y + name_bar_h + 15 + 18 + 8 + 16;  // status_y + 8px gap + font size
            vita2d_font_draw_text(font, hint_x, hint_y, hint_color, 16, console->host->status_hint);
        } else {
            console->host->status_hint[0] = '\0';
            console->host->status_hint_is_error = false;
            console->host->status_hint_expire_us = 0;
        }
    }
}

void ui_cards_render_grid(void) {
    // Update cache (respects 10-second interval)
    ui_cards_update_cache(false);

    int num_cards = card_cache.num_cards;

    // Update card focus animation
    int focused_index = ui_focus_is_content() ? selected_console_index : -1;
    update_card_focus_animation(focused_index);

    // --- Cooldown banner (rendered first so it's behind cards) ---
    uint64_t now_us = sceKernelGetProcessTimeWide();
    uint64_t cooldown_until_us = stream_cooldown_until_us();
    bool cooldown_active = cooldown_until_us && cooldown_until_us > now_us;
    if (!cooldown_active &&
        context.stream.disconnect_banner_until_us &&
        context.stream.disconnect_banner_until_us <= now_us) {
        context.stream.disconnect_reason[0] = '\0';
        context.stream.disconnect_banner_until_us = 0;
    }

    if (cooldown_active) {
        const char *reason =
            (context.stream.disconnect_reason[0] &&
             context.stream.disconnect_banner_until_us > now_us)
                ? context.stream.disconnect_reason
                : "Connection interrupted";
        char banner_text[196];
        sceClibSnprintf(banner_text, sizeof(banner_text),
                        "Streaming stopped: %s - Please wait a few moments",
                        reason);
        int banner_w = VITA_WIDTH;
        int banner_h = 44;
        vita2d_draw_rectangle(0, 0, banner_w, banner_h,
                              RGBA8(0x05, 0x05, 0x07, 235));
        int banner_text_w = vita2d_font_text_width(font, FONT_SIZE_BODY, banner_text);
        int banner_text_x = (banner_w - banner_text_w) / 2;
        int banner_text_y = banner_h / 2 + (FONT_SIZE_BODY / 2) - 4;
        vita2d_font_draw_text(font, banner_text_x, banner_text_y,
                              UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, banner_text);
    }

    /* --- Horizontal layout math --- */
    int visible = num_cards < CARDS_VISIBLE_MAX ? num_cards : CARDS_VISIBLE_MAX;
    int row_width = (num_cards > 0) ? (visible * CONSOLE_CARD_WIDTH + (visible - 1) * CARD_H_GAP) : 0;

    /* Content area: from nav bar to screen edge */
    int content_center_x = ui_get_dynamic_content_center_x();
    int start_x = content_center_x - (row_width / 2);

    /* Vertically center cards */
    int card_y = (VITA_HEIGHT / 2) - (CONSOLE_CARD_HEIGHT / 2);

    /* Header text */
    const char* header_text = "Which do you want to connect?";
    int text_width = vita2d_font_text_width(font, 24, header_text);
    int text_x = content_center_x - (text_width / 2);
    int text_y = card_y - 50;
    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 24, header_text);

    /* --- Poll IME dialog if running --- */
    ui_cards_poll_filter_ime();

    /* --- Filter bar --- */
    if (filter_active) {
        char filter_bar[80];
        sceClibSnprintf(filter_bar, sizeof(filter_bar), "Filter: \"%s\" (%d found)", filter_text, num_cards);
        int fb_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, filter_bar);
        int fb_x = content_center_x - fb_w / 2;
        int fb_y = text_y + 28;
        vita2d_font_draw_text(font, fb_x, fb_y, UI_COLOR_PRIMARY_BLUE, FONT_SIZE_SMALL, filter_bar);
    } else if (num_cards > CARDS_VISIBLE_MAX) {
        const char* hint = "Start: Search";
        int hint_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
        int hint_x = content_center_x - hint_w / 2;
        int hint_y = text_y + 28;
        vita2d_font_draw_text(font, hint_x, hint_y, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);
    }

    /* Show empty state message if no cards */
    if (num_cards == 0) {
        const char* empty_msg = filter_active ? "No consoles match filter" : "Searching for consoles...";
        int em_w = vita2d_font_text_width(font, FONT_SIZE_BODY, empty_msg);
        int em_x = content_center_x - em_w / 2;
        int em_y = (VITA_HEIGHT / 2) + 10;
        vita2d_font_draw_text(font, em_x, em_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, empty_msg);
        return;
    }

    // --- Animate scroll ---
    float anim_offset = update_scroll_animation();

    // Card stride = card width + gap
    int stride = CONSOLE_CARD_WIDTH + CARD_H_GAP;

    // Calculate pixel offset from animation
    float base_pixel_x = (float)start_x - anim_offset * (float)stride;

    VitaChiakiHost *cooldown_host = cooldown_active ? context.active_host : NULL;

    // Render visible cards (draw one extra on each side for smooth scroll-in)
    int draw_start = (int)anim_offset - 1;
    if (draw_start < 0) draw_start = 0;
    int draw_end = (int)anim_offset + visible + 1;
    if (draw_end > num_cards) draw_end = num_cards;

    for (int i = draw_start; i < draw_end; i++) {
        int card_x = (int)(base_pixel_x + (float)(i * stride));

        // Skip cards fully off-screen
        if (card_x + CONSOLE_CARD_WIDTH < 0 || card_x > VITA_WIDTH)
            continue;

        bool selected = (i == selected_console_index && ui_focus_is_content());
        bool card_cooldown = cooldown_host &&
                             card_cache.cards[i].host == cooldown_host;
        float scale = get_card_scale(i, selected);

        ui_cards_render_single(&card_cache.cards[i], card_x, card_y, selected,
                               card_cooldown, scale);
    }

    // --- Scroll arrows (drawn when more cards exist off-screen) ---
    if (num_cards > CARDS_VISIBLE_MAX) {
        int arrow_y = card_y + CONSOLE_CARD_HEIGHT / 2;
        uint32_t arrow_color = RGBA8(200, 200, 200, 180);

        // Left arrow (if not at start)
        if (scroll_offset > 0) {
            int lx = start_x - 30;
            // Draw simple left triangle
            vita2d_draw_rectangle(lx, arrow_y - 6, 12, 12, arrow_color);
        }

        // Right arrow (if not at end)
        if (scroll_offset + CARDS_VISIBLE_MAX < num_cards) {
            int rx = start_x + row_width + 18;
            vita2d_draw_rectangle(rx, arrow_y - 6, 12, 12, arrow_color);
        }

        // Page indicator dots
        int total_pages = (num_cards + CARDS_VISIBLE_MAX - 1) / CARDS_VISIBLE_MAX;
        int current_page = scroll_offset / CARDS_VISIBLE_MAX;
        // Clamp: if scroll_offset doesn't align to pages, use selected card's page
        if (total_pages > 1) {
            current_page = selected_console_index / CARDS_VISIBLE_MAX;
            int dot_spacing = 14;
            int dots_width = total_pages * dot_spacing;
            int dots_x = content_center_x - dots_width / 2;
            int dots_y = card_y + CONSOLE_CARD_HEIGHT + 25;

            for (int p = 0; p < total_pages; p++) {
                uint32_t dot_color = (p == current_page)
                    ? UI_COLOR_PRIMARY_BLUE
                    : RGBA8(120, 120, 120, 150);
                int dx = dots_x + p * dot_spacing + dot_spacing / 2;
                ui_draw_circle(dx, dots_y, 3, dot_color);
            }
        }
    }
}

// ============================================================================
// Selection & State Accessors
// ============================================================================

int ui_cards_get_selected_index(void) {
    return selected_console_index;
}

void ui_cards_set_selected_index(int index) {
    selected_console_index = index;
}

int ui_cards_get_count(void) {
    return card_cache.num_cards;
}

void ui_cards_ensure_selected_visible(void) {
    int num_cards = card_cache.num_cards;
    if (num_cards <= CARDS_VISIBLE_MAX) {
        if (scroll_offset != 0)
            start_scroll_animation(0);
        return;
    }

    // If selected card is left of visible window, scroll left
    if (selected_console_index < scroll_offset) {
        start_scroll_animation(selected_console_index);
    }
    // If selected card is right of visible window, scroll right
    else if (selected_console_index >= scroll_offset + CARDS_VISIBLE_MAX) {
        start_scroll_animation(selected_console_index - CARDS_VISIBLE_MAX + 1);
    }
}

ConsoleCardInfo* ui_cards_get_selected_card(void) {
    if (card_cache.num_cards == 0)
        return NULL;
    if (selected_console_index < 0 || selected_console_index >= card_cache.num_cards)
        return NULL;
    return &card_cache.cards[selected_console_index];
}

int ui_cards_get_scroll_offset(void) {
    return scroll_offset;
}
