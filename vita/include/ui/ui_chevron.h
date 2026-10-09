/**
 * @file ui_chevron.h
 * @brief Baked left and right chevrons (SPEC.md C08 ChoiceValue, section 4.1 back chevron)
 *
 * The mock's chevron on its 24 x 24 grid, baked once at any size as a white texture and tinted at
 * draw time. Used by the Controller page (preset switcher and back chevron).
 */

#pragma once

#include <vita2d.h>

/** Which way a chevron points. */
typedef enum ui_chevron_dir_t {
  UI_CHEVRON_LEFT = 0,
  UI_CHEVRON_RIGHT,
} UiChevronDir;

/**
 * ui_chevron_bake() - Bake a chevron into a new @art x @art white texture.
 * @dir:    Which way it points.
 * @art:    Side of the square texture in pixels.
 * @stroke: Line thickness in texture pixels, with round ends.
 *
 * @return the texture, owned by the caller for the life of the app, or NULL when it could not be
 *         allocated (logged by ui_bake_white())
 */
vita2d_texture *ui_chevron_bake(UiChevronDir dir, int art, float stroke);
