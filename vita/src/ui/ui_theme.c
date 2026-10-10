/**
 * @file ui_theme.c
 * @brief The five XMB colour presets and the accessor for the current one (issue #348)
 *
 * Source of truth: docs/design/ui-mocks/tokens-themes.css (Ember, Orchid, Moss, Graphite) and the
 * :root block of tokens-xmb.css (Ocean). Each entry below is one preset block of those files.
 */

#include "ui/ui_theme.h"

#include "context.h"

/** One entry per VitaChiakiTheme, indexed by the enum value. */
static const UiTheme THEMES[VITA_THEME_COUNT] =
    {
        [VITA_THEME_OCEAN] =
            {
                .wave_top = {6, 32, 74},
                .wave_mid = {15, 69, 133},
                .wave_bottom = {36, 95, 156},
                .ribbon_1 = {150, 205, 255},
                .ribbon_2 = {100, 180, 240},
                .ribbon_3 = {190, 220, 255},
                .horizon = {160, 210, 255},
                .glyph_bg_a = {10, 20, 40},
                .glyph_bg_b = {43, 70, 102},
                .ink = {2, 5, 14},
                .wash = {3, 6, 16},
                .deep = {6, 12, 28},
                .deep_edge = {4, 8, 20},
                .accent = {109, 180, 255},
                .accent_hi = {160, 205, 255},
                .halo = {130, 170, 255},
            },
        [VITA_THEME_EMBER] =
            {
                .wave_top = {52, 15, 4},
                .wave_mid = {133, 42, 15},
                .wave_bottom = {158, 64, 36},
                .ribbon_1 = {255, 173, 148},
                .ribbon_2 = {239, 130, 97},
                .ribbon_3 = {255, 204, 189},
                .horizon = {255, 181, 158},
                .glyph_bg_a = {29, 16, 12},
                .glyph_bg_b = {86, 56, 46},
                .ink = {16, 5, 2},
                .wash = {19, 7, 4},
                .deep = {29, 12, 6},
                .deep_edge = {19, 7, 4},
                .accent = {255, 142, 107},
                .accent_hi = {255, 181, 158},
                .halo = {255, 157, 128},
            },
        [VITA_THEME_ORCHID] =
            {
                .wave_top = {47, 5, 61},
                .wave_mid = {104, 15, 133},
                .wave_bottom = {127, 36, 158},
                .ribbon_1 = {228, 148, 255},
                .ribbon_2 = {204, 97, 239},
                .ribbon_3 = {238, 189, 255},
                .horizon = {231, 158, 255},
                .glyph_bg_a = {24, 12, 29},
                .glyph_bg_b = {76, 46, 86},
                .ink = {12, 2, 16},
                .wash = {15, 4, 19},
                .deep = {24, 6, 29},
                .deep_edge = {15, 4, 19},
                .accent = {218, 107, 255},
                .accent_hi = {231, 158, 255},
                .halo = {223, 128, 255},
            },
        [VITA_THEME_MOSS] =
            {
                .wave_top = {3, 33, 13},
                .wave_mid = {10, 92, 37},
                .wave_bottom = {25, 108, 52},
                .ribbon_1 = {148, 255, 184},
                .ribbon_2 = {97, 239, 145},
                .ribbon_3 = {189, 255, 211},
                .horizon = {158, 255, 190},
                .glyph_bg_a = {12, 29, 18},
                .glyph_bg_b = {46, 86, 60},
                .ink = {2, 16, 7},
                .wash = {4, 19, 9},
                .deep = {6, 29, 14},
                .deep_edge = {4, 19, 9},
                .accent = {107, 255, 156},
                .accent_hi = {158, 255, 190},
                .halo = {128, 255, 170},
            },
        [VITA_THEME_GRAPHITE] =
            {
                .wave_top = {25, 27, 31},
                .wave_mid = {67, 70, 81},
                .wave_bottom = {90, 93, 104},
                .ribbon_1 = {195, 198, 208},
                .ribbon_2 = {160, 164, 177},
                .ribbon_3 = {218, 220, 226},
                .horizon = {201, 204, 212},
                .glyph_bg_a = {15, 16, 27},
                .glyph_bg_b = {60, 64, 74},
                .ink = {8, 9, 10},
                .wash = {11, 11, 12},
                .deep = {16, 17, 19},
                .deep_edge = {11, 11, 12},
                .accent = {172, 177, 190},
                .accent_hi = {201, 204, 212},
                .halo = {184, 187, 199},
            },
};

const UiTheme *ui_theme_current(void) {
  VitaChiakiTheme theme = context.config.theme;
  if ((unsigned)theme >= (unsigned)VITA_THEME_COUNT)
    theme = VITA_THEME_OCEAN;
  return &THEMES[theme];
}
