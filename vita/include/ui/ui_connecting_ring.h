/**
 * @file ui_connecting_ring.h
 * @brief C03 ConnectingRing: the ring around the room icon, with its halo (SPEC.md C03)
 *
 * Display-only draw helper. The ring (2 px) and its 12 px glow are one white texture padded by
 * the glow radius (the Glow rule), tinted by the state colour; the halo is a second baked
 * texture. Cost: 2 draws plus the room icon. The 176 px spinner is drawn by the screen after
 * this (ui_spinner.h).
 */

#pragma once

#include <vita2d.h>

/** State colour of the ring. */
typedef enum ui_ring_state_t {
  UI_RING_STATE_OK = 0,
  UI_RING_STATE_WARN,
  UI_RING_STATE_ERR,
} UiRingState;

/** ui_connecting_ring_init() - Bake the ring and the halo. Call once at start-up. */
void ui_connecting_ring_init(void);

/** ui_draw_halo() - Draw the soft halo centred at (@cx, @cy): 1 draw. Drawn first by the ring;
 * the Reconnecting screen draws it on its own. */
void ui_draw_halo(int cx, int cy);

/**
 * ui_draw_connecting_ring() - Draw the halo, the ring and the room icon.
 * @x, @y:  Top-left corner of the ring's @size x @size box. The halo and the glow reach past it.
 * @size:   Ring size; the art is baked at UI_RING_SIZE and scaled to this.
 * @room:   Room icon, white, at least UI_RING_SIZE * UI_RING_ICON_PCT / 100 px wide; NULL draws
 *          no icon.
 * @state:  Colour of the ring and its glow.
 */
void ui_draw_connecting_ring(int x, int y, int size, vita2d_texture *room, UiRingState state);
