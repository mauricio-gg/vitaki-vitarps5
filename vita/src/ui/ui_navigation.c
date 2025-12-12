/**
 * @file ui_navigation.c
 * @brief Navigation system implementation for VitaRPS5
 *
 * Implements the wave sidebar navigation with:
 * - Collapse/expand animation state machine
 * - Wave animation rendering
 * - Navigation pill (collapsed state)
 * - Touch and button input handling
 * - Procedural icon fallbacks
 *
 * Extracted from ui.c (Phase 5)
 */

#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <string.h>

#include "ui/ui_navigation.h"
#include "ui/ui_internal.h"
#include "ui/ui_constants.h"
#include "ui/ui_graphics.h"
#include "ui/ui_input.h"
#include "context.h"

// ============================================================================
// Internal State
// ============================================================================

// Selected navigation icon and focus state
// Note: Exposed via ui_internal.h for backward compatibility during refactoring
// New code should use ui_nav_get/set functions instead
int selected_nav_icon = 0;   // 0=Play, 1=Settings, 2=Controller, 3=Profile
FocusArea current_focus = FOCUS_CONSOLE_CARDS;
int last_console_selection = 0;  // Remember last selected console when moving away

// Wave animation state
static WaveLayerState wave_bottom_state = {0.0f, WAVE_SPEED_BOTTOM};
static WaveLayerState wave_top_state = {0.0f, WAVE_SPEED_TOP};
static uint64_t wave_last_update_us = 0;  // For delta time calculation

// Navigation collapse state (exposed via ui_internal.h for backward compatibility)
NavCollapseState nav_collapse = {
    .state = NAV_STATE_COLLAPSED,
    .anim_start_us = 0,
    .anim_progress = 0.0f,
    .stored_wave_bottom_phase = 0.0f,
    .stored_wave_top_phase = 0.0f,
    .current_width = 0.0f,
    .pill_width = NAV_PILL_WIDTH,
    .pill_opacity = 1.0f,
    .toast_shown_this_session = false,
    .toast_active = false,
    .toast_start_us = 0
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Ease-in-out cubic interpolation for smooth animation
 */
static inline float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// ============================================================================
// Initialization
// ============================================================================

void ui_nav_init(void) {
    // Reset to default collapsed state
    nav_collapse.state = NAV_STATE_COLLAPSED;
    nav_collapse.anim_start_us = 0;
    nav_collapse.anim_progress = 0.0f;
    nav_collapse.stored_wave_bottom_phase = 0.0f;
    nav_collapse.stored_wave_top_phase = 0.0f;
    nav_collapse.current_width = 0.0f;
    nav_collapse.pill_width = NAV_PILL_WIDTH;
    nav_collapse.pill_opacity = 1.0f;
    nav_collapse.toast_shown_this_session = false;
    nav_collapse.toast_active = false;
    nav_collapse.toast_start_us = 0;

    // Initialize wave animation
    wave_bottom_state.phase = 0.0f;
    wave_bottom_state.speed = WAVE_SPEED_BOTTOM;
    wave_top_state.phase = 0.0f;
    wave_top_state.speed = WAVE_SPEED_TOP;
    wave_last_update_us = 0;

    // Initialize selection and focus
    selected_nav_icon = 0;
    current_focus = FOCUS_CONSOLE_CARDS;
    last_console_selection = 0;
}

// ============================================================================
// State Machine Functions
// ============================================================================

void ui_nav_request_collapse(bool from_content_interaction) {
    // If from content interaction, check if pinned setting blocks it
    if (from_content_interaction && context.config.keep_nav_pinned) {
        return;
    }

    // Only collapse from expanded state
    if (nav_collapse.state != NAV_STATE_EXPANDED) {
        return;
    }

    // Store wave phases for resume
    nav_collapse.stored_wave_bottom_phase = wave_bottom_state.phase;
    nav_collapse.stored_wave_top_phase = wave_top_state.phase;

    // Start collapse animation
    nav_collapse.state = NAV_STATE_COLLAPSING;
    nav_collapse.anim_start_us = sceKernelGetProcessTimeWide();
    nav_collapse.anim_progress = 0.0f;
}

void ui_nav_request_expand(void) {
    // Only expand from collapsed state
    if (nav_collapse.state != NAV_STATE_COLLAPSED) {
        return;
    }

    // Restore wave phases and reset delta timer to prevent phase jump
    wave_bottom_state.phase = nav_collapse.stored_wave_bottom_phase;
    wave_top_state.phase = nav_collapse.stored_wave_top_phase;
    wave_last_update_us = sceKernelGetProcessTimeWide();

    // Start expand animation
    nav_collapse.state = NAV_STATE_EXPANDING;
    nav_collapse.anim_start_us = sceKernelGetProcessTimeWide();
    nav_collapse.anim_progress = 0.0f;
}

void ui_nav_toggle(void) {
    if (nav_collapse.state == NAV_STATE_EXPANDED) {
        ui_nav_request_collapse(false);  // Not from content interaction
    } else if (nav_collapse.state == NAV_STATE_COLLAPSED) {
        ui_nav_request_expand();
    }
    // Ignore toggle during animation
}

void ui_nav_reset_collapsed(void) {
    nav_collapse.state = NAV_STATE_COLLAPSED;
    nav_collapse.anim_progress = 0.0f;
    nav_collapse.current_width = 0.0f;
    nav_collapse.pill_width = NAV_PILL_WIDTH;
    nav_collapse.pill_opacity = 1.0f;
    // Don't reset toast_shown_this_session - should persist for whole app session
}

// ============================================================================
// Toast Functions
// ============================================================================

/**
 * Internal function to show the nav collapse toast
 */
static void internal_show_nav_collapse_toast(void) {
    if (nav_collapse.toast_shown_this_session) {
        return;
    }
    nav_collapse.toast_shown_this_session = true;
    nav_collapse.toast_active = true;
    nav_collapse.toast_start_us = sceKernelGetProcessTimeWide();
}

void ui_nav_update_toast(void) {
    if (!nav_collapse.toast_active) {
        return;
    }

    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed_us = now - nav_collapse.toast_start_us;
    uint64_t total_us = (NAV_TOAST_FADE_MS + NAV_TOAST_DURATION_MS + NAV_TOAST_FADE_MS) * 1000ULL;

    if (elapsed_us >= total_us) {
        nav_collapse.toast_active = false;
    }
}

// ============================================================================
// Animation Update
// ============================================================================

void ui_nav_update_collapse_animation(void) {
    if (nav_collapse.state != NAV_STATE_COLLAPSING && nav_collapse.state != NAV_STATE_EXPANDING) {
        return;
    }

    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed_us = now - nav_collapse.anim_start_us;
    float elapsed_ms = (float)elapsed_us / 1000.0f;
    float progress = elapsed_ms / (float)NAV_COLLAPSE_DURATION_MS;

    if (progress >= 1.0f) {
        progress = 1.0f;

        // Animation complete - transition to final state
        if (nav_collapse.state == NAV_STATE_COLLAPSING) {
            nav_collapse.state = NAV_STATE_COLLAPSED;
            nav_collapse.current_width = 0.0f;
            nav_collapse.pill_width = NAV_PILL_WIDTH;
            nav_collapse.pill_opacity = 1.0f;
            internal_show_nav_collapse_toast();
        } else if (nav_collapse.state == NAV_STATE_EXPANDING) {
            nav_collapse.state = NAV_STATE_EXPANDED;
            nav_collapse.current_width = WAVE_NAV_WIDTH;
            nav_collapse.pill_width = NAV_PILL_HEIGHT;
            nav_collapse.pill_opacity = 0.0f;
        }
    }

    nav_collapse.anim_progress = progress;

    // Calculate interpolated values based on animation state
    if (nav_collapse.state == NAV_STATE_COLLAPSING) {
        // Collapsing: width goes down, pill fades in
        float eased = ease_in_out_cubic(progress);
        nav_collapse.current_width = WAVE_NAV_WIDTH * (1.0f - eased);

        // Pill only appears in phase 3 (200-280ms, progress 0.71-1.0)
        if (progress > 0.71f) {
            float pill_progress = (progress - 0.71f) / 0.29f;
            nav_collapse.pill_width = NAV_PILL_HEIGHT + (NAV_PILL_WIDTH - NAV_PILL_HEIGHT) * pill_progress;
            nav_collapse.pill_opacity = pill_progress;
        } else {
            nav_collapse.pill_width = NAV_PILL_HEIGHT;
            nav_collapse.pill_opacity = 0.0f;
        }
    } else if (nav_collapse.state == NAV_STATE_EXPANDING) {
        // Expanding: reverse of collapsing
        // Pill contracts first (0-80ms, progress 0-0.29)
        if (progress < 0.29f) {
            float pill_progress = 1.0f - (progress / 0.29f);
            nav_collapse.pill_width = NAV_PILL_HEIGHT + (NAV_PILL_WIDTH - NAV_PILL_HEIGHT) * pill_progress;
            nav_collapse.pill_opacity = pill_progress;
            nav_collapse.current_width = 0.0f;
        } else {
            // Sidebar expands (80-280ms, progress 0.29-1.0)
            nav_collapse.pill_width = NAV_PILL_HEIGHT;
            nav_collapse.pill_opacity = 0.0f;
            float width_progress = (progress - 0.29f) / 0.71f;
            nav_collapse.current_width = WAVE_NAV_WIDTH * width_progress;
        }
    }
}

void ui_nav_update_wave_animation(void) {
    uint64_t now_us = sceKernelGetProcessTimeWide();
    if (wave_last_update_us == 0) {
        wave_last_update_us = now_us;
        return;
    }

    float delta_sec = (float)(now_us - wave_last_update_us) / 1000000.0f;
    wave_last_update_us = now_us;

    // Update wave phases
    wave_bottom_state.phase += wave_bottom_state.speed * delta_sec;
    wave_top_state.phase += wave_top_state.speed * delta_sec;

    // Wrap phases to prevent float overflow, using a large period for seamless looping.
    // We wrap at 1000*2*PI (~6283 radians) to ensure long-running animation remains
    // smooth and within float precision limits.
    float wrap_period = 1000.0f * 2.0f * M_PI;
    wave_bottom_state.phase = fmodf(wave_bottom_state.phase, wrap_period);
    wave_top_state.phase = fmodf(wave_top_state.phase, wrap_period);
}

// ============================================================================
// Procedural Icon Drawing
// ============================================================================

void ui_nav_draw_hamburger_icon(int x, int cy, int size, uint32_t color) {
    int line_h = 2;
    int line_w = size;

    // Position three lines evenly: top at -size/2, middle at center, bottom at +size/2
    int y1 = cy - size / 2 + line_h / 2;
    int y2 = cy;
    int y3 = cy + size / 2 - line_h / 2;

    vita2d_draw_rectangle(x, y1 - line_h / 2, line_w, line_h, color);
    vita2d_draw_rectangle(x, y2 - line_h / 2, line_w, line_h, color);
    vita2d_draw_rectangle(x, y3 - line_h / 2, line_w, line_h, color);
}

void ui_nav_draw_play_icon(int cx, int cy, int size) {
    uint32_t white = RGBA8(255, 255, 255, 255);
    int half_size = size / 2;

    // Triangle centroid is at 1/3 from left edge for proper visual centering
    // Offset the triangle left by size/6 to center it visually
    int offset = size / 6;

    // Draw filled triangle using horizontal lines
    // Triangle points adjusted for visual centering
    for (int y = -half_size; y <= half_size; y++) {
        int x_start = cx - half_size + abs(y) - offset;  // Left edge moves right as we go away from center
        int x_end = cx + half_size - offset;              // Right edge is fixed
        int width = x_end - x_start;
        if (width > 0) {
            vita2d_draw_rectangle(x_start, cy + y, width, 1, white);
        }
    }
}

void ui_nav_draw_settings_icon(int cx, int cy, int size) {
    uint32_t white = RGBA8(255, 255, 255, 255);
    int outer_r = size / 2;
    int inner_r = size / 4;
    int tooth_count = 8;

    // Draw center circle
    ui_draw_circle(cx, cy, inner_r, white);

    // Draw gear teeth as small rectangles around the perimeter
    for (int i = 0; i < tooth_count; i++) {
        float angle = (float)i * 2.0f * M_PI / (float)tooth_count;
        int tooth_x = cx + (int)(cosf(angle) * (outer_r - 3));
        int tooth_y = cy + (int)(sinf(angle) * (outer_r - 3));
        // Draw small square tooth
        vita2d_draw_rectangle(tooth_x - 3, tooth_y - 3, 6, 6, white);
    }

    // Draw outer ring using line segments
    int segments = 32;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i * 2.0f * M_PI / (float)segments;
        float a2 = (float)(i + 1) * 2.0f * M_PI / (float)segments;
        int x1 = cx + (int)(cosf(a1) * (outer_r - 5));
        int y1 = cy + (int)(sinf(a1) * (outer_r - 5));
        int x2 = cx + (int)(cosf(a2) * (outer_r - 5));
        int y2 = cy + (int)(sinf(a2) * (outer_r - 5));
        vita2d_draw_line(x1, y1, x2, y2, white);
    }
}

void ui_nav_draw_controller_icon(int cx, int cy, int size) {
    uint32_t white = RGBA8(255, 255, 255, 255);
    int w = size;
    int h = size * 2 / 3;

    // Main body (rounded rectangle approximation)
    int body_x = cx - w / 2;
    int body_y = cy - h / 3;
    ui_draw_rounded_rect(body_x, body_y, w, h, 4, white);

    // Left handle
    int handle_w = w / 4;
    int handle_h = h / 2;
    ui_draw_rounded_rect(body_x - handle_w / 3, body_y + h / 3, handle_w, handle_h, 3, white);

    // Right handle
    ui_draw_rounded_rect(body_x + w - handle_w + handle_w / 3, body_y + h / 3, handle_w, handle_h, 3, white);

    // D-pad (left side) - draw as cross
    int dpad_x = body_x + w / 4;
    int dpad_y = body_y + h / 2;
    int dpad_size = 3;
    vita2d_draw_rectangle(dpad_x - dpad_size, dpad_y - 1, dpad_size * 2, 2, UI_COLOR_CARD_BG);
    vita2d_draw_rectangle(dpad_x - 1, dpad_y - dpad_size, 2, dpad_size * 2, UI_COLOR_CARD_BG);

    // Buttons (right side) - draw as small circles
    int btn_x = body_x + w * 3 / 4;
    int btn_y = body_y + h / 2;
    ui_draw_circle(btn_x, btn_y - 3, 2, UI_COLOR_CARD_BG);
    ui_draw_circle(btn_x, btn_y + 3, 2, UI_COLOR_CARD_BG);
    ui_draw_circle(btn_x - 3, btn_y, 2, UI_COLOR_CARD_BG);
    ui_draw_circle(btn_x + 3, btn_y, 2, UI_COLOR_CARD_BG);
}

void ui_nav_draw_profile_icon(int cx, int cy, int size) {
    uint32_t white = RGBA8(255, 255, 255, 255);

    // Head (circle at top)
    int head_r = size / 4;
    int head_y = cy - size / 6;
    ui_draw_circle(cx, head_y, head_r, white);

    // Body (arc/shoulders) - approximate with rounded rectangle
    int body_w = size * 2 / 3;
    int body_h = size / 3;
    int body_x = cx - body_w / 2;
    int body_y = cy + size / 8;
    ui_draw_rounded_rect(body_x, body_y, body_w, body_h, body_h / 2, white);
}

// ============================================================================
// Pill Rendering
// ============================================================================

void ui_nav_render_pill(void) {
    if (nav_collapse.pill_opacity <= 0.0f) {
        return;
    }

    int x = NAV_PILL_X;
    int y = NAV_PILL_Y;
    int w = (int)nav_collapse.pill_width;
    int h = NAV_PILL_HEIGHT;
    int r = h / 2;  // Fully rounded ends

    // Calculate alpha from pill_opacity (90% max opacity)
    uint8_t alpha = (uint8_t)(nav_collapse.pill_opacity * 230);
    uint32_t bg_color = RGBA8(0x2D, 0x32, 0x37, alpha);

    // Focus highlight (if pill is focused while collapsed)
    bool pill_focused = (nav_collapse.state == NAV_STATE_COLLAPSED &&
                         current_focus == FOCUS_NAV_BAR);
    if (pill_focused) {
        ui_draw_rounded_rect(x - 2, y - 2, w + 4, h + 4, r + 2,
                             UI_COLOR_PRIMARY_BLUE);
    }

    // Pill background
    ui_draw_rounded_rect(x, y, w, h, r, bg_color);

    // Hamburger icon + "Menu" text (centered together as a single unit)
    if (w > 50) {
        int hamburger_size = 14;
        int menu_text_width = vita2d_font_text_width(font, FONT_SIZE_BODY, "Menu");
        int gap = 8;
        int total_content_width = hamburger_size + gap + menu_text_width;
        int content_start_x = x + (w - total_content_width) / 2;

        int icon_cy = y + h / 2;
        uint8_t icon_alpha = (uint8_t)(nav_collapse.pill_opacity * 255);
        ui_nav_draw_hamburger_icon(content_start_x, icon_cy, hamburger_size,
                                    RGBA8(250, 250, 250, icon_alpha));

        if (w >= NAV_PILL_WIDTH - 10) {
            uint8_t text_alpha = (uint8_t)(nav_collapse.pill_opacity * 255);
            int text_x = content_start_x + hamburger_size + gap;
            vita2d_font_draw_text(font, text_x, y + h / 2 + 5,
                                  RGBA8(250, 250, 250, text_alpha),
                                  FONT_SIZE_BODY, "Menu");
        }
    }
}

void ui_nav_render_toast(void) {
    if (!nav_collapse.toast_active) {
        return;
    }

    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed_us = now - nav_collapse.toast_start_us;
    uint64_t elapsed_ms = elapsed_us / 1000ULL;

    // Calculate opacity based on animation phase
    float opacity = 1.0f;
    if (elapsed_ms < NAV_TOAST_FADE_MS) {
        // Fade in
        opacity = (float)elapsed_ms / (float)NAV_TOAST_FADE_MS;
    } else if (elapsed_ms > NAV_TOAST_FADE_MS + NAV_TOAST_DURATION_MS) {
        // Fade out
        uint64_t fade_elapsed = elapsed_ms - NAV_TOAST_FADE_MS - NAV_TOAST_DURATION_MS;
        opacity = 1.0f - ((float)fade_elapsed / (float)NAV_TOAST_FADE_MS);
    }

    if (opacity <= 0.0f) {
        return;
    }

    const char* text = "Menu hidden - tap pill or press Triangle to reopen";
    int text_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, text);

    int toast_x = NAV_PILL_X;
    int toast_y = NAV_PILL_Y + NAV_PILL_HEIGHT + 8;
    int toast_w = text_w + 24;
    int toast_h = 28;

    uint8_t alpha = (uint8_t)(opacity * 230);
    uint32_t bg_color = RGBA8(0x2D, 0x32, 0x37, alpha);
    uint32_t text_color = RGBA8(250, 250, 250, (uint8_t)(opacity * 255));

    ui_draw_rounded_rect(toast_x, toast_y, toast_w, toast_h, 8, bg_color);
    vita2d_font_draw_text(font, toast_x + 12, toast_y + toast_h / 2 + 4,
                          text_color, FONT_SIZE_SMALL, text);
}

void ui_nav_render_content_overlay(void) {
    if (nav_collapse.state != NAV_STATE_EXPANDED) {
        return;
    }
    vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT,
                          RGBA8(0, 0, 0, 80));
}

// ============================================================================
// Main Rendering
// ============================================================================

void ui_nav_render(void) {
    // Update collapse animation state first
    ui_nav_update_collapse_animation();
    ui_nav_update_toast();

    // If fully collapsed, render only the pill and toast (save GPU cycles)
    if (nav_collapse.state == NAV_STATE_COLLAPSED) {
        ui_nav_render_pill();
        ui_nav_render_toast();
        return;
    }

    // Update wave animation state (only when not collapsed)
    if (nav_collapse.state == NAV_STATE_EXPANDED) {
        ui_nav_update_wave_animation();
    }

    // Calculate width scale for animation
    float width_scale = nav_collapse.current_width / (float)WAVE_NAV_WIDTH;
    if (width_scale < 0.01f) {
        // Nearly collapsed - just render pill during transition
        ui_nav_render_pill();
        ui_nav_render_toast();
        return;
    }

    // Draw procedural fluid wave background with continuous vertical animation
    // Base teal colors matching PlayStation aesthetic
    // Note: No solid background rectangle - waves extend fully to screen top

    // Draw multiple sine wave layers that create fluid motion
    // Using wave phase from ui_nav_update_wave_animation() for time-based animation
    for (int layer = 0; layer < 2; layer++) {
        // Each layer has different speed and amplitude for depth
        float phase = (layer == 0) ? wave_bottom_state.phase : wave_top_state.phase;
        float amplitude = (12.0f + (float)layer * 8.0f) * width_scale;

        // Draw wave as filled polygon using horizontal slices
        for (int y = 0; y < VITA_HEIGHT; y++) {
            // Calculate wave X offset at this Y position
            // Multiple frequencies create complex wave pattern
            float wave_x = sinf((float)y * 0.015f + phase) * amplitude +
                           sinf((float)y * 0.008f - phase * 0.7f) * (amplitude * 0.5f);

            // Allow waves to extend freely beyond current width
            int right_edge = (int)(nav_collapse.current_width + wave_x);

            // Clamp to prevent negative width and excessive overdraw
            if (right_edge < 0) right_edge = 0;
            if (right_edge > (int)(nav_collapse.current_width) + 50) {
                right_edge = (int)(nav_collapse.current_width) + 50;
            }

            // Draw horizontal slice with layered alpha for depth
            uint8_t alpha = (layer == 0) ? WAVE_ALPHA_BOTTOM : WAVE_ALPHA_TOP;
            uint32_t wave_color = RGBA8(90, 150, 160, alpha);
            vita2d_draw_rectangle(0, y, right_edge, 1, wave_color);
        }
    }

    // Calculate icon opacity for fade effect during collapse
    float icon_opacity = width_scale;
    if (icon_opacity > 1.0f) icon_opacity = 1.0f;
    if (icon_opacity < 0.0f) icon_opacity = 0.0f;

    // Draw navigation icons (fade during collapse animation)
    for (int i = 0; i < 4; i++) {
        // Base Y position
        int base_y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);

        // Icons are static (no bobbing animation)
        int y = base_y;

        bool is_selected = (i == selected_nav_icon && current_focus == FOCUS_NAV_BAR);

        // Selection highlight (semi-transparent white rounded rect per spec line 76)
        // Only show when not animating and sidebar is expanded
        if (is_selected && nav_collapse.state == NAV_STATE_EXPANDED) {
            int highlight_size = 48;
            int highlight_x = WAVE_NAV_ICON_X - highlight_size / 2;
            int highlight_y = y - highlight_size / 2;
            // White at 20% alpha as specified
            ui_draw_rounded_rect(highlight_x, highlight_y, highlight_size, highlight_size, 8, RGBA8(255, 255, 255, 51));
        }

        // Draw icon with scale increase when selected
        float icon_scale_multiplier = is_selected ? 1.15f : 1.0f;  // 15% larger when selected

        // Use texture-based icons (fallback to procedural if NULL)
        vita2d_texture* icon_tex = NULL;
        switch (i) {
            case 0: icon_tex = icon_play; break;
            case 1: icon_tex = icon_settings; break;
            case 2: icon_tex = icon_controller; break;
            case 3: icon_tex = icon_profile; break;
        }

        if (icon_tex) {
            // Texture-based rendering: single draw call per icon
            int tex_w = vita2d_texture_get_width(icon_tex);
            int tex_h = vita2d_texture_get_height(icon_tex);
            float scale = (WAVE_NAV_ICON_SIZE * icon_scale_multiplier) / (float)tex_w;
            int scaled_w = (int)(tex_w * scale);
            int scaled_h = (int)(tex_h * scale);
            int draw_x = WAVE_NAV_ICON_X - scaled_w / 2;
            int draw_y = y - scaled_h / 2;

            // Apply opacity during animation (tint with alpha)
            if (icon_opacity < 1.0f) {
                uint8_t tint_alpha = (uint8_t)(icon_opacity * 255);
                vita2d_draw_texture_tint_scale(icon_tex, draw_x, draw_y, scale, scale,
                                               RGBA8(255, 255, 255, tint_alpha));
            } else {
                vita2d_draw_texture_scale(icon_tex, draw_x, draw_y, scale, scale);
            }
        } else {
            // Fallback to procedural icons if texture failed to load
            int current_icon_size = (int)(WAVE_NAV_ICON_SIZE * icon_scale_multiplier);
            switch (i) {
                case 0: ui_nav_draw_play_icon(WAVE_NAV_ICON_X, y, current_icon_size); break;
                case 1: ui_nav_draw_settings_icon(WAVE_NAV_ICON_X, y, current_icon_size); break;
                case 2: ui_nav_draw_controller_icon(WAVE_NAV_ICON_X, y, current_icon_size); break;
                case 3: ui_nav_draw_profile_icon(WAVE_NAV_ICON_X, y, current_icon_size); break;
            }
        }
    }

    // Render pill during expanding animation (fades out as sidebar expands)
    if (nav_collapse.state == NAV_STATE_EXPANDING) {
        ui_nav_render_pill();
    }

    // Toast may still be visible during expansion
    ui_nav_render_toast();
}

// ============================================================================
// State Queries
// ============================================================================

bool ui_nav_is_expanded(void) {
    return nav_collapse.state == NAV_STATE_EXPANDED;
}

bool ui_nav_is_collapsed(void) {
    return nav_collapse.state == NAV_STATE_COLLAPSED;
}

bool ui_nav_is_animating(void) {
    return nav_collapse.state == NAV_STATE_COLLAPSING ||
           nav_collapse.state == NAV_STATE_EXPANDING;
}

float ui_nav_get_current_width(void) {
    return nav_collapse.current_width;
}

NavSidebarState ui_nav_get_state(void) {
    return nav_collapse.state;
}

// ============================================================================
// Selection & Focus
// ============================================================================

int ui_nav_get_selected_icon(void) {
    return selected_nav_icon;
}

void ui_nav_set_selected_icon(int index) {
    if (index >= 0 && index < 4) {
        selected_nav_icon = index;
    }
}

UIScreenType ui_nav_screen_for_icon(int index) {
    switch (index) {
        case 0: return UI_SCREEN_TYPE_MAIN;
        case 1: return UI_SCREEN_TYPE_SETTINGS;
        case 2: return UI_SCREEN_TYPE_CONTROLLER;
        case 3: return UI_SCREEN_TYPE_PROFILE;
        default: return UI_SCREEN_TYPE_MAIN;
    }
}

FocusArea ui_nav_get_focus(void) {
    return current_focus;
}

void ui_nav_set_focus(FocusArea focus) {
    current_focus = focus;
}

int ui_nav_get_last_console_selection(void) {
    return last_console_selection;
}

void ui_nav_set_last_console_selection(int index) {
    last_console_selection = index;
}

// ============================================================================
// Input Handling
// ============================================================================

bool ui_nav_handle_touch(float touch_x, float touch_y, UIScreenType *out_screen) {
    for (int i = 0; i < 4; i++) {
        int icon_x = WAVE_NAV_ICON_X;
        int icon_y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);
        if (is_point_in_circle(touch_x, touch_y, icon_x, icon_y, 30)) {
            selected_nav_icon = i;
            current_focus = FOCUS_NAV_BAR;
            if (out_screen)
                *out_screen = ui_nav_screen_for_icon(i);
            return true;
        }
    }
    return false;
}

bool ui_nav_handle_pill_touch(float touch_x, float touch_y) {
    if (nav_collapse.state != NAV_STATE_COLLAPSED) {
        return false;
    }

    float x = NAV_PILL_X;
    float y = NAV_PILL_Y;
    float w = nav_collapse.pill_width;
    float h = NAV_PILL_HEIGHT;

    // Expand hitbox slightly for easier touch (8px padding)
    float pad = 8.0f;
    return (touch_x >= x - pad && touch_x <= x + w + pad &&
            touch_y >= y - pad && touch_y <= y + h + pad);
}

bool ui_nav_handle_shortcuts(UIScreenType *out_screen, bool allow_dpad) {
    // Triangle button toggles sidebar collapse (global, works anywhere)
    if (btn_pressed(SCE_CTRL_TRIANGLE)) {
        ui_nav_toggle();
        // Don't return - let other input processing continue
    }

    SceTouchData nav_touch = {};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &nav_touch, 1);

    // Get touch block state from ui_input module
    bool* touch_block_active = ui_input_get_touch_block_active_ptr();
    bool* touch_block_pending_clear = ui_input_get_touch_block_pending_clear_ptr();

    if (*touch_block_active) {
        if (nav_touch.reportNum == 0) {
            // Finger lifted - clear the block
            *touch_block_active = false;
            *touch_block_pending_clear = false;
        } else {
            return false;  // Still blocking while finger is down
        }
    }

    if (nav_touch.reportNum > 0) {
        float tx = (nav_touch.report[0].x / 1920.0f) * 960.0f;
        float ty = (nav_touch.report[0].y / 1088.0f) * 544.0f;

        // Check pill touch first when collapsed
        if (ui_nav_handle_pill_touch(tx, ty)) {
            ui_nav_request_expand();
            *touch_block_active = true;  // Prevent immediate re-collapse
            return false;
        }

        // If expanded, check nav icon touch
        if (nav_collapse.state == NAV_STATE_EXPANDED) {
            if (ui_nav_handle_touch(tx, ty, out_screen)) {
                *touch_block_active = true;
                return true;
            }
        }

        // Touch in content area triggers collapse (if not pinned)
        // Content area is to the right of the nav bar
        if (nav_collapse.state == NAV_STATE_EXPANDED && tx > WAVE_NAV_WIDTH && !*touch_block_active) {
            ui_nav_request_collapse(true);  // From content interaction
            *touch_block_active = true;   // Prevent double-processing of this touch
        }
    }

    if (!allow_dpad)
        return false;

    // D-pad handling depends on collapse state
    if (nav_collapse.state == NAV_STATE_COLLAPSED) {
        // When collapsed, D-pad Left focuses pill (already focused by default)
        // Cross/Confirm on pill expands sidebar
        if (current_focus == FOCUS_NAV_BAR) {
            if (btn_pressed(SCE_CTRL_CROSS) || btn_pressed(SCE_CTRL_LEFT)) {
                ui_nav_request_expand();
                return false;
            }
            // D-pad Right moves to content and keeps sidebar collapsed
            if (btn_pressed(SCE_CTRL_RIGHT)) {
                current_focus = FOCUS_CONSOLE_CARDS;
            }
        } else {
            // Focus is on content - D-pad Left focuses pill
            if (btn_pressed(SCE_CTRL_LEFT)) {
                current_focus = FOCUS_NAV_BAR;
            }
        }
        return false;
    }

    // Normal expanded state D-pad handling
    if (btn_pressed(SCE_CTRL_LEFT)) {
        current_focus = FOCUS_NAV_BAR;
    } else if (btn_pressed(SCE_CTRL_RIGHT) && current_focus == FOCUS_NAV_BAR) {
        current_focus = FOCUS_CONSOLE_CARDS;
        // Moving focus to content triggers collapse
        ui_nav_request_collapse(true);
    }

    if (current_focus == FOCUS_NAV_BAR) {
        if (btn_pressed(SCE_CTRL_UP)) {
            selected_nav_icon = (selected_nav_icon - 1 + 4) % 4;
        } else if (btn_pressed(SCE_CTRL_DOWN)) {
            selected_nav_icon = (selected_nav_icon + 1) % 4;
        }

        if (btn_pressed(SCE_CTRL_CROSS)) {
            if (out_screen)
                *out_screen = ui_nav_screen_for_icon(selected_nav_icon);
            return true;
        }
    }

    return false;
}
