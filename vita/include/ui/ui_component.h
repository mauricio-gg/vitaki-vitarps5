/**
 * @file ui_component.h
 * @brief Component model for the XMB UI (SPEC.md section 2.0)
 *
 * Interactive components are a plain struct plus three functions:
 *   void    ui_thing_init(UiThing *, ...);          sets data, computes rects once
 *   void    ui_thing_draw(const UiThing *);         no state change, no allocation
 *   UiEvent ui_thing_input(UiThing *, const UiInput *);
 * Display-only components are plain draw helpers. Screens own their components as
 * struct members, forward input to them and decide draw order.
 *
 * Every interactive component stores a visible rect (drawing) and a hit rect (touch),
 * computed at init. The hit rect is the visible rect grown to at least UI_TAP_MIN.
 *
 * This header has no SDK dependency so the rect helpers can be checked natively.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Geometry
 * ============================================================================ */

/** Axis-aligned rectangle in screen pixels. */
typedef struct ui_rect_t {
  int x;
  int y;
  int w;
  int h;
} UiRect;

/**
 * ui_rect_hit_from_visible() - Grow a visible rect to a touch-sized hit rect.
 * @visible: The rect that is drawn.
 * @min_w:   Minimum hit width.
 * @min_h:   Minimum hit height.
 *
 * The result is centred on @visible and at least @min_w x @min_h; a dimension that
 * is already large enough is left unchanged.
 */
static inline UiRect ui_rect_hit_from_visible(UiRect visible, int min_w, int min_h) {
  UiRect hit = visible;
  if (visible.w < min_w) {
    hit.x = visible.x - (min_w - visible.w) / 2;
    hit.w = min_w;
  }
  if (visible.h < min_h) {
    hit.y = visible.y - (min_h - visible.h) / 2;
    hit.h = min_h;
  }
  return hit;
}

/** ui_rect_contains() - True when the point lies inside @r (right and bottom edges excluded). */
static inline bool ui_rect_contains(UiRect r, float px, float py) {
  return px >= (float)r.x && px < (float)(r.x + r.w) && py >= (float)r.y && py < (float)(r.y + r.h);
}

/**
 * ui_color_scale_alpha() - Scale the alpha byte of an ABGR colour.
 * @color: Colour whose alpha is scaled.
 * @k:     Factor, 0 (invisible) to 1 (unchanged).
 */
static inline uint32_t ui_color_scale_alpha(uint32_t color, float k) {
  uint32_t alpha = (uint32_t)((float)(color >> 24) * k + 0.5f);
  return (color & 0x00FFFFFFu) | (alpha << 24);
}

/**
 * ui_layer_set_alpha() - Set the opacity every UI draw helper multiplies into its colours.
 * @k: 0 (invisible) to 1 (unchanged, the default).
 *
 * A screen sets it before drawing the layers that must be dimmed or faded as one (the Home
 * layers behind the Options column, a popup while it rises) and back to 1 afterwards. It is a
 * tint on draws that happen anyway, never an extra draw. The text, shape, glow and logo helpers
 * and the dimmed layers' own texture draws apply it through ui_layer_color().
 */
void ui_layer_set_alpha(float k);

/** ui_layer_color() - @color with the current layer opacity multiplied into its alpha. */
uint32_t ui_layer_color(uint32_t color);

/* ============================================================================
 * Events and input
 * ============================================================================ */

/** What a component's input function reports back to its screen. */
typedef enum ui_event_t {
  UI_EVENT_NONE = 0,
  UI_EVENT_MOVED,     /**< focus moved to another item */
  UI_EVENT_ACTIVATED, /**< the focused item was confirmed or tapped */
  UI_EVENT_CANCELLED, /**< the user backed out */
} UiEvent;

/**
 * Logical buttons, already resolved by the Circle Button Confirm setting:
 * CONFIRM and CANCEL are Cross/Circle (or swapped); screens never test the setting.
 */
typedef enum ui_button_t {
  UI_BTN_CONFIRM = 1u << 0,
  UI_BTN_CANCEL = 1u << 1,
  UI_BTN_OPTIONS = 1u << 2, /**< Triangle */
  UI_BTN_CLEAR = 1u << 3,   /**< Square */
  UI_BTN_FILTER = 1u << 4,  /**< Start */
  UI_BTN_BROWSER = 1u << 5, /**< Select */
  UI_BTN_L = 1u << 6,
  UI_BTN_R = 1u << 7,
  UI_BTN_UP = 1u << 8,
  UI_BTN_DOWN = 1u << 9,
  UI_BTN_LEFT = 1u << 10,
  UI_BTN_RIGHT = 1u << 11,
} UiButton;

/** Mask of the four D-pad directions (the buttons that hold-repeat). */
#define UI_BTN_DPAD (UI_BTN_UP | UI_BTN_DOWN | UI_BTN_LEFT | UI_BTN_RIGHT)

/** Front-panel touch in screen pixels (960 x 544). */
typedef struct ui_touch_t {
  float x;
  float y;
  bool down;     /**< a finger is on the panel and this touch is not blocked */
  bool pressed;  /**< touch-down edge */
  bool released; /**< touch-up edge */
  bool dragged;  /**< the finger moved more than UI_TOUCH_DRAG_PX from touch-down; never a tap */
  float dx;      /**< movement since touch-down */
  float dy;
} UiTouch;

/** One per-frame input snapshot, built once at the top of the UI frame. */
typedef struct ui_input_t {
  uint32_t pressed;  /**< UiButton bits: newly pressed this frame */
  uint32_t down;     /**< UiButton bits: held */
  uint32_t released; /**< UiButton bits: released this frame */
  uint32_t repeat;   /**< UI_BTN_DPAD bits that fire this frame: the press edge, then hold-repeat */
  UiTouch touch;
} UiInput;

/** ui_touch_tap() - True on the frame a finger lifts without having dragged. */
static inline bool ui_touch_tap(const UiInput *in) {
  return in->touch.released && !in->touch.dragged;
}

/* ============================================================================
 * Shared draw resources
 * ============================================================================ */

struct vita2d_texture;

/**
 * ui_glow_init() - Bake the shared white glow texture once.
 *
 * A UI_LIST_GLOW square, fully transparent at its border (SPEC glow rule), drawn
 * before the art it lights and tinted to the wanted alpha. Safe to call twice.
 */
void ui_glow_init(void);

/** ui_glow_texture() - The shared glow texture, or NULL if ui_glow_init() failed. */
struct vita2d_texture *ui_glow_texture(void);

/**
 * ui_glow_draw_rect() - Draw the shared glow stretched behind a rectangle (one draw).
 * @around: The rect (text box, button) the glow lights.
 * @pad:    How far the glow reaches past @around on every side.
 * @color:  ABGR tint; its alpha is the glow's strength.
 *
 * For glows behind text and buttons, where the art is wider than it is tall. Drawn before the
 * art; the texture's transparent border keeps it from ever showing an edge.
 */
void ui_glow_draw_rect(UiRect around, int pad, uint32_t color);
