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

#include "ui/ui_internal.h"
#include "ui/ui_console_cards.h"
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
// Forward Declarations
// ============================================================================

static void update_card_focus_animation(int new_focus_index);
static float get_card_scale(int card_index, bool is_focused);

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

    // Count current valid hosts
    int num_hosts = 0;
    ConsoleCardInfo temp_cards[MAX_NUM_HOSTS];

    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
        if (context.hosts[i]) {
            ui_cards_map_host(context.hosts[i], &temp_cards[num_hosts]);
            num_hosts++;
        }
    }

    // Only update cache if we have valid hosts (prevents storing empty state during discovery updates)
    if (num_hosts > 0) {
        card_cache.num_cards = num_hosts;
        memcpy(card_cache.cards, temp_cards, sizeof(ConsoleCardInfo) * num_hosts);
        card_cache.last_update_time = current_time;
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
    // Center cards within content area (830px starting at x=130)
    int content_center_x = ui_get_dynamic_content_center_x();
    int screen_center_y = VITA_HEIGHT / 2;

    // Update cache (respects 10-second interval)
    ui_cards_update_cache(false);

    // Update card focus animation
    int focused_index = (current_focus == FOCUS_CONSOLE_CARDS) ? selected_console_index : -1;
    update_card_focus_animation(focused_index);

    // Calculate card position - centered within content area
    int card_y = screen_center_y - (CONSOLE_CARD_HEIGHT / 2);
    int card_x = content_center_x - (CONSOLE_CARD_WIDTH / 2);

    // Header text - centered within content area above the card
    const char* header_text = "Which do you want to connect?";
    int text_width = vita2d_font_text_width(font, 24, header_text);
    int text_x = content_center_x - (text_width / 2);
    int text_y = card_y - 50;  // Position text 50px above card

    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 24, header_text);

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
        int banner_x = 0;
        int banner_y = 0;
        vita2d_draw_rectangle(banner_x, banner_y, banner_w, banner_h,
                              RGBA8(0x05, 0x05, 0x07, 235));
        int banner_text_w = vita2d_font_text_width(font, FONT_SIZE_BODY, banner_text);
        int banner_text_x = banner_x + (banner_w - banner_text_w) / 2;
        int banner_text_y = banner_y + banner_h / 2 + (FONT_SIZE_BODY / 2) - 4;
        vita2d_font_draw_text(font, banner_text_x, banner_text_y,
                              UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, banner_text);
    }

    // Use cached cards to prevent flickering
    if (card_cache.num_cards > 0) {
        VitaChiakiHost *cooldown_host = cooldown_active ? context.active_host : NULL;
        for (int i = 0; i < card_cache.num_cards; i++) {
            // For multiple cards, stack them vertically centered around screen center
            int this_card_y = card_y + (i * CONSOLE_CARD_SPACING);

            // Only show selection highlight if console cards have focus
            bool selected = (i == selected_console_index && current_focus == FOCUS_CONSOLE_CARDS);
            bool card_cooldown = cooldown_host &&
                                 card_cache.cards[i].host == cooldown_host;

            // Get animated scale for this card
            float scale = get_card_scale(i, selected);

            ui_cards_render_single(&card_cache.cards[i], card_x, this_card_y, selected,
                                   card_cooldown, scale);
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
