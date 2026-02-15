/**
 * @file ui_components.c
 * @brief Reusable UI widgets and dialogs implementation for VitaRPS5
 *
 * This module implements high-level UI components used throughout the
 * VitaRPS5 interface. All components follow the PlayStation design language
 * with smooth animations and consistent styling.
 *
 * Extracted from ui.c during Phase 4 of UI refactoring.
 */

#include "context.h"
#include "ui/ui_components.h"
#include "ui/ui_internal.h"
#include "ui/ui_graphics.h"
#include "ui/ui_focus.h"
#include "video.h"

#include <math.h>
#include <psp2/kernel/clib.h>

// ============================================================================
// Internal State
// ============================================================================

// Toggle animation state
static ToggleAnimationState toggle_anim = {-1, false, 0};

// Hints popup state
static HintsPopupState hints_popup = {0};

// Debug menu configuration
const bool debug_menu_enabled = VITARPS5_DEBUG_MENU != 0;
const uint32_t DEBUG_MENU_COMBO_MASK =
    SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START;
const char *debug_menu_options[] = {
    "Show Remote Play error popup",
    "Simulate disconnect banner",
    "Trigger network unstable badge",
};

// PIN digit constants
#define PIN_DIGIT_WIDTH 60
#define PIN_DIGIT_HEIGHT 70

// Cursor blink state (for PIN entry) - defined in ui.c, declared in ui_internal.h

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * Linear interpolation
 */
static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/**
 * Ease-in-out cubic interpolation for smooth animation
 */
static inline float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// ============================================================================
// Widget Drawing Functions
// ============================================================================

/**
 * Draw an animated toggle switch control
 */
void ui_draw_toggle_switch(int x, int y, int width, int height, float anim_value, bool selected) {
    // Interpolate track color based on animation value
    uint32_t color_off = RGBA8(0x60, 0x60, 0x68, 200);
    uint32_t color_on = UI_COLOR_PRIMARY_BLUE;

    // Blend between OFF and ON colors
    uint8_t r = (uint8_t)lerp(0x60, 0x34, anim_value);
    uint8_t g = (uint8_t)lerp(0x60, 0x90, anim_value);
    uint8_t b = (uint8_t)lerp(0x68, 0xFF, anim_value);
    uint8_t a = (uint8_t)lerp(200, 255, anim_value);
    uint32_t track_color = RGBA8(r, g, b, a);

    uint32_t knob_color = UI_COLOR_TEXT_PRIMARY;

    // Enhanced selection highlight with glow
    if (selected) {
        // Outer glow
        ui_draw_rounded_rect(x - 3, y - 3, width + 6, height + 6, height/2 + 2, RGBA8(0x34, 0x90, 0xFF, 60));
        // Border
        ui_draw_rounded_rect(x - 2, y - 2, width + 4, height + 4, height/2 + 1, UI_COLOR_PRIMARY_BLUE);
    }

    // Track shadow for depth
    ui_draw_rounded_rect(x + 1, y + 1, width, height, height/2, RGBA8(0x00, 0x00, 0x00, 40));

    // Track (background)
    ui_draw_rounded_rect(x, y, width, height, height/2, track_color);

    // Knob (circular button) - smoothly animated position
    int knob_radius = (height - 4) / 2;
    int knob_x_off = x + knob_radius + 2;
    int knob_x_on = x + width - knob_radius - 2;
    int knob_x = (int)lerp((float)knob_x_off, (float)knob_x_on, anim_value);
    int knob_y = y + height/2;

    // Knob shadow
    ui_draw_circle(knob_x + 1, knob_y + 1, knob_radius, RGBA8(0x00, 0x00, 0x00, 80));
    // Knob
    ui_draw_circle(knob_x, knob_y, knob_radius, knob_color);
}

/**
 * Draw a dropdown control with label and current value
 */
void ui_draw_dropdown(int x, int y, int width, int height, const char* label,
                      const char* value, bool expanded, bool selected) {
    // Modern card colors with subtle variation for selection
    uint32_t bg_color = selected ? RGBA8(0x40, 0x42, 0x50, 255) : UI_COLOR_CARD_BG;

    // Enhanced selection with shadow and glow
    if (selected && !expanded) {
        // Shadow
        ui_draw_rounded_rect(x + 2, y + 2, width, height, 8, RGBA8(0x00, 0x00, 0x00, 60));
        // Outer glow
        ui_draw_rounded_rect(x - 3, y - 3, width + 6, height + 6, 10, RGBA8(0x34, 0x90, 0xFF, 50));
        // Border
        ui_draw_rounded_rect(x - 2, y - 2, width + 4, height + 4, 10, UI_COLOR_PRIMARY_BLUE);
    } else {
        // Subtle shadow for depth
        ui_draw_rounded_rect(x + 1, y + 1, width, height, 8, RGBA8(0x00, 0x00, 0x00, 30));
    }

    // Background
    ui_draw_rounded_rect(x, y, width, height, 8, bg_color);

    // Label text (left)
    vita2d_font_draw_text(font, x + 15, y + height/2 + 6, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, label);

    // Value text (right)
    int value_width = vita2d_font_text_width(font, FONT_SIZE_BODY, value);
    vita2d_font_draw_text(font, x + width - value_width - 30, y + height/2 + 6,
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, value);

    // Down arrow indicator - enhanced with PlayStation Blue when selected
    int arrow_x = x + width - 18;
    int arrow_y = y + height/2;
    int arrow_size = 6;
    uint32_t arrow_color = selected ? UI_COLOR_PRIMARY_BLUE : UI_COLOR_TEXT_SECONDARY;

    // Draw downward pointing triangle
    for (int i = 0; i < arrow_size; i++) {
        vita2d_draw_rectangle(arrow_x - i, arrow_y + i, 1 + i*2, 1, arrow_color);
    }
}

/**
 * Draw a tabbed navigation bar with color-coded sections
 */
void ui_draw_tab_bar(int x, int y, int width, int height,
                     const char* tabs[], uint32_t colors[], int num_tabs, int selected) {
    int tab_width = width / num_tabs;

    for (int i = 0; i < num_tabs; i++) {
        int tab_x = x + (i * tab_width);

        // Tab background - flat color, no dimming
        ui_draw_rounded_rect(tab_x, y, tab_width - 4, height, 8, colors[i]);

        // Tab text (centered)
        int text_width = vita2d_font_text_width(font, FONT_SIZE_SUBHEADER, tabs[i]);
        int text_x = tab_x + (tab_width - text_width) / 2;
        int text_y = y + height/2 + 6;

        vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, tabs[i]);

        // Selection indicator (bottom bar) - only visual difference
        if (i == selected) {
            vita2d_draw_rectangle(tab_x + 2, y + height - 3, tab_width - 8, 3, UI_COLOR_PRIMARY_BLUE);
        }
    }
}

/**
 * Draw a colored status indicator dot
 */
void ui_draw_status_dot(int x, int y, int radius, UIStatusType status) {
    uint32_t color;
    switch (status) {
        case UI_STATUS_ACTIVE:
            color = RGBA8(0x2D, 0x8A, 0x3E, 255); // Green
            break;
        case UI_STATUS_STANDBY:
            color = RGBA8(0xD9, 0x77, 0x06, 255); // Orange/Yellow
            break;
        case UI_STATUS_ERROR:
            color = RGBA8(0xDC, 0x26, 0x26, 255); // Red
            break;
        default:
            color = RGBA8(0x80, 0x80, 0x80, 255); // Gray
    }

    ui_draw_circle(x, y, radius, color);
}

/**
 * Draw a styled section header with title and accent line
 */
void ui_draw_section_header(int x, int y, int width, const char* title) {
    // Subtle gradient background bar
    int header_h = 40;
    ui_draw_rounded_rect(x, y, width, header_h, 8, RGBA8(0x30, 0x35, 0x40, 200));

    // Bottom accent line (PlayStation Blue)
    vita2d_draw_rectangle(x, y + header_h - 2, width, 2, UI_COLOR_PRIMARY_BLUE);

    // Title text (centered vertically in header)
    vita2d_font_draw_text(font, x + 15, y + (header_h / 2) + 8, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, title);
}

/**
 * Draw a single PIN entry digit box
 */
void ui_draw_pin_digit(int x, int y, uint32_t digit, bool is_current, bool has_value) {
    // Enhanced visual feedback for current digit
    if (is_current) {
        // Outer glow effect for better visibility
        ui_draw_rounded_rect(x - 2, y - 2, PIN_DIGIT_WIDTH + 4, PIN_DIGIT_HEIGHT + 4, 6, RGBA8(0x34, 0x90, 0xFF, 60));
    }

    // Digit box background with shadow
    int shadow_offset = is_current ? 3 : 2;
    ui_draw_rounded_rect(x + shadow_offset, y + shadow_offset, PIN_DIGIT_WIDTH, PIN_DIGIT_HEIGHT, 4, RGBA8(0x00, 0x00, 0x00, 60));

    uint32_t box_color = is_current ? UI_COLOR_PRIMARY_BLUE : RGBA8(0x2C, 0x2C, 0x2E, 255);
    ui_draw_rounded_rect(x, y, PIN_DIGIT_WIDTH, PIN_DIGIT_HEIGHT, 4, box_color);

    // Digit text or cursor
    if (has_value && digit <= 9) {
        char digit_text[2] = {'0' + digit, '\0'};
        int text_w = vita2d_font_text_width(font, 40, digit_text);
        int text_x = x + (PIN_DIGIT_WIDTH / 2) - (text_w / 2);
        int text_y = y + (PIN_DIGIT_HEIGHT / 2) + 15;
        vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 40, digit_text);
    } else if (is_current && show_cursor) {
        // Enhanced blinking cursor (wider and more visible)
        int cursor_w = 3;
        int cursor_x = x + (PIN_DIGIT_WIDTH / 2) - (cursor_w / 2);
        int cursor_y1 = y + 15;
        int cursor_h = PIN_DIGIT_HEIGHT - 30;
        vita2d_draw_rectangle(cursor_x, cursor_y1, cursor_w, cursor_h, UI_COLOR_TEXT_PRIMARY);
    }
}

// ============================================================================
// Toggle Switch Animation
// ============================================================================

/**
 * Start toggle switch animation
 */
void ui_toggle_start_animation(int toggle_index, bool target_state) {
    toggle_anim.animating_index = toggle_index;
    toggle_anim.target_state = target_state;
    toggle_anim.start_time_us = sceKernelGetProcessTimeWide();
}

/**
 * Get current animation value for a toggle switch
 */
float ui_toggle_get_animation_value(int toggle_index, bool current_state) {
    // If not animating this toggle, return static value
    if (toggle_anim.animating_index != toggle_index) {
        return current_state ? 1.0f : 0.0f;
    }

    // Calculate animation progress
    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed_us = now - toggle_anim.start_time_us;
    float progress = (float)elapsed_us / (TOGGLE_ANIMATION_DURATION_MS * 1000.0f);

    // Clamp to 0.0-1.0
    if (progress >= 1.0f) {
        toggle_anim.animating_index = -1;  // Animation complete
        return toggle_anim.target_state ? 1.0f : 0.0f;
    }

    // Apply easing for smooth motion
    float eased = ease_in_out_cubic(progress);

    // Interpolate from start to end
    float start_val = toggle_anim.target_state ? 0.0f : 1.0f;
    float end_val = toggle_anim.target_state ? 1.0f : 0.0f;

    return lerp(start_val, end_val, eased);
}

// ============================================================================
// Error Popup Dialog
// ============================================================================

/**
 * Show error popup with specified message
 */
void ui_error_show(const char* message) {
    context.ui_state.error_popup_active = true;
    if (message) {
        sceClibSnprintf(context.ui_state.error_popup_text,
                        sizeof(context.ui_state.error_popup_text),
                        "%s", message);
    } else {
        context.ui_state.error_popup_text[0] = '\0';
    }

    // Push modal focus once per popup activation.
    if (!context.ui_state.error_popup_modal_pushed) {
        ui_focus_push_modal();
        context.ui_state.error_popup_modal_pushed = true;
    }
}

/**
 * Hide the error popup
 */
void ui_error_hide(void) {
  context.ui_state.error_popup_active = false;
  context.ui_state.error_popup_text[0] = '\0';

  // Pop only if this popup owns a modal push.
  if (context.ui_state.error_popup_modal_pushed) {
    ui_focus_pop_modal();
    context.ui_state.error_popup_modal_pushed = false;
  }
}

/**
 * Render the error popup
 */
void ui_error_render(void) {
    if (!context.ui_state.error_popup_active)
        return;

    // Semi-transparent overlay
    vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT, RGBA8(0, 0, 0, 120));

    // Popup card
    const int popup_w = 520;
    const int popup_h = 280;
    int popup_x = (VITA_WIDTH - popup_w) / 2;
    int popup_y = (VITA_HEIGHT - popup_h) / 2;
    ui_draw_rounded_rect(popup_x, popup_y, popup_w, popup_h, 16,
                         RGBA8(0x14, 0x16, 0x1C, 240));

    // Error message text
    const char *message = context.ui_state.error_popup_text[0]
                              ? context.ui_state.error_popup_text
                              : "Connection error";
    int message_w = vita2d_font_text_width(font, FONT_SIZE_HEADER, message);
    int message_x = popup_x + (popup_w - message_w) / 2;
    int message_y = popup_y + popup_h / 2;
    vita2d_font_draw_text(font, message_x, message_y,
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, message);

    // Hint text
    const char *hint = "Tap anywhere to dismiss";
    int hint_w = vita2d_font_text_width(font, FONT_SIZE_BODY, hint);
    int hint_x = popup_x + (popup_w - hint_w) / 2;
    int hint_y = popup_y + popup_h - 40;
    vita2d_font_draw_text(font, hint_x, hint_y,
                          UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, hint);
}

/**
 * Handle input for error popup
 */
void ui_error_handle_input(void) {
    if (!context.ui_state.error_popup_active)
        return;

    // Get button block mask and touch block pointers
    uint32_t* button_block_mask = ui_input_get_button_block_mask_ptr();
    bool* touch_block_active = ui_input_get_touch_block_active_ptr();

    uint32_t dismiss_mask = SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START | SCE_CTRL_SELECT;
    bool button_dismiss =
        (context.ui_state.button_state & dismiss_mask) &&
        !(context.ui_state.old_button_state & dismiss_mask);
    bool touch_dismiss = context.ui_state.touch_state_front.reportNum > 0;

    if (button_dismiss || touch_dismiss) {
        ui_error_hide();
        *button_block_mask |= context.ui_state.button_state;
        *touch_block_active = true;
    }
}

/**
 * Check if error popup is currently active
 */
bool ui_error_is_active(void) {
    return context.ui_state.error_popup_active;
}

// ============================================================================
// Hints Popup System
// ============================================================================

/**
 * Trigger hints popup with specified hint text
 */
void ui_hints_trigger(const char* hint_text) {
    hints_popup.active = true;
    hints_popup.start_time_us = sceKernelGetProcessTimeWide();
    hints_popup.current_hint = hint_text;
}

/**
 * Render the hints popup
 */
void ui_hints_render(void) {
    if (!hints_popup.active || !hints_popup.current_hint)
        return;

    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed_us = now - hints_popup.start_time_us;
    float elapsed_ms = elapsed_us / 1000.0f;

    // Calculate opacity with fade out
    float opacity = 1.0f;
    if (elapsed_ms > HINTS_POPUP_DURATION_MS - HINTS_FADE_DURATION_MS) {
        float fade_progress = (elapsed_ms - (HINTS_POPUP_DURATION_MS - HINTS_FADE_DURATION_MS)) / HINTS_FADE_DURATION_MS;
        opacity = 1.0f - fade_progress;
        if (opacity < 0.0f) opacity = 0.0f;
    }

    // Deactivate when duration complete
    if (elapsed_ms >= HINTS_POPUP_DURATION_MS) {
        hints_popup.active = false;
        return;
    }

    // Render hint pill at bottom of screen
    int text_width = vita2d_font_text_width(font, FONT_SIZE_SMALL, hints_popup.current_hint);
    int pill_w = text_width + 40;
    int pill_h = 36;
    int pill_x = (VITA_WIDTH - pill_w) / 2;
    int pill_y = VITA_HEIGHT - pill_h - 20;

    uint8_t alpha = (uint8_t)(opacity * 200);
    ui_draw_rounded_rect(pill_x, pill_y, pill_w, pill_h, 18, RGBA8(0, 0, 0, alpha));

    int text_x = pill_x + 20;
    int text_y = pill_y + pill_h / 2 + 5;
    vita2d_font_draw_text(font, text_x, text_y, RGBA8(255, 255, 255, alpha), FONT_SIZE_SMALL, hints_popup.current_hint);
}

/**
 * Render hints indicator in top-right corner
 */
void ui_hints_render_indicator(void) {
    const char* indicator = "(Select) Hints";
    int text_width = vita2d_font_text_width(font, FONT_SIZE_SMALL, indicator);
    int text_x = VITA_WIDTH - text_width - 100;  // Left of logo
    int text_y = 35;
    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, indicator);
}

// ============================================================================
// Debug Menu (VITARPS5_DEBUG_MENU must be enabled)
// ============================================================================

// Forward declare helper function
static void ensure_active_host_for_debug(void);

/**
 * Ensure there's an active host for debug actions
 */
static void ensure_active_host_for_debug(void) {
    if (context.active_host)
        return;
    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
        if (context.hosts[i]) {
            context.active_host = context.hosts[i];
            break;
        }
    }
}

/**
 * Apply selected debug menu action
 */
static void debug_menu_apply_action(int action_index) {
    if (!debug_menu_enabled)
        return;
    if (action_index < 0 || action_index >= DEBUG_MENU_OPTION_COUNT)
        return;

    switch (action_index) {
        case 0: {
            // Show Remote Play error popup
            ui_error_show("Remote Play already active on console");
            LOGD("Debug menu: forced Remote Play error popup");
            break;
        }
        case 1: {
            // Simulate disconnect banner
            ensure_active_host_for_debug();
            uint64_t now_us = sceKernelGetProcessTimeWide();
            const uint64_t demo_duration_us = 4 * 1000 * 1000ULL;
            sceClibSnprintf(context.stream.disconnect_reason,
                            sizeof(context.stream.disconnect_reason),
                            "Connection interrupted (debug)");
            context.stream.disconnect_banner_until_us = now_us + demo_duration_us;
            context.stream.next_stream_allowed_us = now_us + demo_duration_us;
            LOGD("Debug menu: simulated disconnect banner for %llums",
                 (unsigned long long)(demo_duration_us / 1000ULL));
            break;
        }
        case 2: {
            // Trigger network unstable badge
            uint64_t now_us = sceKernelGetProcessTimeWide();
            const uint64_t alert_duration_us = 3 * 1000 * 1000ULL;
            context.stream.loss_alert_duration_us = alert_duration_us;
            context.stream.loss_alert_until_us = now_us + alert_duration_us;
            vitavideo_show_poor_net_indicator();
            LOGD("Debug menu: triggered network unstable indicator for %llums",
                 (unsigned long long)(alert_duration_us / 1000ULL));
            break;
        }
        default:
            break;
    }

    ui_debug_close();
}

/**
 * Open the debug menu
 */
void ui_debug_open(void) {
    if (!debug_menu_enabled)
        return;
    if (context.ui_state.debug_menu_active)
        return;

    context.ui_state.debug_menu_active = true;
    context.ui_state.debug_menu_selection = 0;

    // Block inputs
    uint32_t* button_block_mask = ui_input_get_button_block_mask_ptr();
    bool* touch_block_active = ui_input_get_touch_block_active_ptr();
    *button_block_mask |= context.ui_state.button_state;
    *touch_block_active = true;

    // Push modal focus once per debug menu activation.
    if (!context.ui_state.debug_menu_modal_pushed) {
        ui_focus_push_modal();
        context.ui_state.debug_menu_modal_pushed = true;
    }
}

/**
 * Close the debug menu
 */
void ui_debug_close(void) {
    if (!context.ui_state.debug_menu_active)
        return;

    context.ui_state.debug_menu_active = false;
    context.ui_state.debug_menu_selection = 0;

    // Block inputs
    uint32_t* button_block_mask = ui_input_get_button_block_mask_ptr();
    bool* touch_block_active = ui_input_get_touch_block_active_ptr();
    *button_block_mask |= context.ui_state.button_state;
    *touch_block_active = true;

  // Pop only if this menu owns a modal push.
  if (context.ui_state.debug_menu_modal_pushed) {
    ui_focus_pop_modal();
    context.ui_state.debug_menu_modal_pushed = false;
  }
}

/**
 * Render the debug menu
 */
void ui_debug_render(void) {
    if (!context.ui_state.debug_menu_active)
        return;

    // Semi-transparent overlay
    vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT, RGBA8(0, 0, 0, 120));

    // Panel dimensions
    const int panel_w = 560;
    const int panel_h = 240;
    int panel_x = (VITA_WIDTH - panel_w) / 2;
    int panel_y = (VITA_HEIGHT - panel_h) / 2;
    ui_draw_rounded_rect(panel_x, panel_y, panel_w, panel_h, 18,
                         RGBA8(0x14, 0x16, 0x1C, 240));

    // Title
    const char *title = "Debug Actions";
    int title_w = vita2d_font_text_width(font, FONT_SIZE_HEADER, title);
    vita2d_font_draw_text(font,
                          panel_x + (panel_w - title_w) / 2,
                          panel_y + 40,
                          UI_COLOR_TEXT_PRIMARY,
                          FONT_SIZE_HEADER,
                          title);

    // Option list
    int list_y = panel_y + 70;
    for (int i = 0; i < DEBUG_MENU_OPTION_COUNT; i++) {
        uint32_t row_color = RGBA8(0x30, 0x35, 0x40, 255);
        if (i == context.ui_state.debug_menu_selection) {
            row_color = RGBA8(0x34, 0x90, 0xFF, 160);
        }
        int row_h = 44;
        int row_margin = 6;
        ui_draw_rounded_rect(panel_x + 30,
                             list_y + i * (row_h + row_margin),
                             panel_w - 60,
                             row_h,
                             10,
                             row_color);
        vita2d_font_draw_text(font,
                              panel_x + 50,
                              list_y + i * (row_h + row_margin) + row_h / 2 + 6,
                              UI_COLOR_TEXT_PRIMARY,
                              FONT_SIZE_BODY,
                              debug_menu_options[i]);
    }

    // Hint text
    const char *hint = "D-Pad: Select  |  X: Trigger  |  Circle: Close";
    int hint_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
    vita2d_font_draw_text(font,
                          panel_x + (panel_w - hint_w) / 2,
                          panel_y + panel_h - 20,
                          UI_COLOR_TEXT_SECONDARY,
                          FONT_SIZE_SMALL,
                          hint);
}

/**
 * Handle input for debug menu
 */
void ui_debug_handle_input(void) {
    if (!context.ui_state.debug_menu_active)
        return;

    uint32_t buttons = context.ui_state.button_state;
    uint32_t prev_buttons = context.ui_state.old_button_state;

    // Navigate up
    if ((buttons & SCE_CTRL_UP) && !(prev_buttons & SCE_CTRL_UP)) {
        context.ui_state.debug_menu_selection--;
        if (context.ui_state.debug_menu_selection < 0)
            context.ui_state.debug_menu_selection = DEBUG_MENU_OPTION_COUNT - 1;
    }
    // Navigate down
    else if ((buttons & SCE_CTRL_DOWN) && !(prev_buttons & SCE_CTRL_DOWN)) {
        context.ui_state.debug_menu_selection++;
        if (context.ui_state.debug_menu_selection >= DEBUG_MENU_OPTION_COUNT)
            context.ui_state.debug_menu_selection = 0;
    }
    // Trigger action
    else if ((buttons & SCE_CTRL_CROSS) && !(prev_buttons & SCE_CTRL_CROSS)) {
        debug_menu_apply_action(context.ui_state.debug_menu_selection);
    }
    // Close menu
    else if ((buttons & SCE_CTRL_CIRCLE) && !(prev_buttons & SCE_CTRL_CIRCLE)) {
        ui_debug_close();
    }
}

/**
 * Check if debug menu is currently active
 */
bool ui_debug_is_active(void) {
    return context.ui_state.debug_menu_active;
}

// ============================================================================
// Legacy Compatibility Wrappers (for ui.c internal use)
// ============================================================================

// These static wrappers maintain backwards compatibility with existing ui.c code
// Once ui.c is fully refactored, these can be removed

void draw_toggle_switch(int x, int y, int width, int height, float anim_value, bool selected) {
    ui_draw_toggle_switch(x, y, width, height, anim_value, selected);
}

void draw_dropdown(int x, int y, int width, int height, const char* label,
                   const char* value, bool expanded, bool selected) {
    ui_draw_dropdown(x, y, width, height, label, value, expanded, selected);
}

void draw_tab_bar(int x, int y, int width, int height,
                  const char* tabs[], uint32_t colors[], int num_tabs, int selected) {
    ui_draw_tab_bar(x, y, width, height, tabs, colors, num_tabs, selected);
}

void draw_status_dot(int x, int y, int radius, int status) {
    ui_draw_status_dot(x, y, radius, (UIStatusType)status);
}

void draw_section_header(int x, int y, int width, const char* title) {
    ui_draw_section_header(x, y, width, title);
}

void render_pin_digit(int x, int y, uint32_t digit, bool is_current, bool has_value) {
    ui_draw_pin_digit(x, y, digit, is_current, has_value);
}

void start_toggle_animation(int toggle_index, bool target_state) {
    ui_toggle_start_animation(toggle_index, target_state);
}

float get_toggle_animation_value(int toggle_index, bool current_state) {
    return ui_toggle_get_animation_value(toggle_index, current_state);
}

void render_error_popup(void) {
    ui_error_render();
}

void handle_error_popup_input(void) {
    ui_error_handle_input();
}

void trigger_hints_popup(const char* hint_text) {
    ui_hints_trigger(hint_text);
}

void render_hints_popup(void) {
    ui_hints_render();
}

void render_hints_indicator(void) {
    ui_hints_render_indicator();
}

void open_debug_menu(void) {
    ui_debug_open();
}

void close_debug_menu(void) {
    ui_debug_close();
}

void render_debug_menu(void) {
    ui_debug_render();
}

void handle_debug_menu_input(void) {
    ui_debug_handle_input();
}
