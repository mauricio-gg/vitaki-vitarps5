# VitaRPS5 Patches to libvita2d

Vendored from xerpi/libvita2d master branch.
Only `vita2d_font.c` and `texture_atlas.c` are vendored; all other
libvita2d symbols come from the system `-lvita2d`.

---

## Active patch — 2× supersampled atlas + LINEAR/LINEAR filters

**Files:**
- `vita2d_font.c` — `generic_font_draw_text()` renders glyphs into the
  atlas at `size * VITARPS5_FONT_SUPERSAMPLE` (= 2× the requested point
  size) and stores them with `glyph_size = size * 2`. The atlas is
  enlarged to 1024×1024 to fit 4× the glyph area.
- `texture_atlas.c` — `texture_atlas_create()` calls
  `vita2d_texture_set_filters(atlas->texture, LINEAR, LINEAR)`.

**How the pieces fit together:**

The existing `draw_scale = size / (float)data.glyph_size` in
`generic_font_draw_text()` evaluates to exactly `0.5` for every glyph
under this patch. All position math (`pen_x + bitmap_left * draw_scale`,
`pen_y - bitmap_top * draw_scale`, `(advance_x >> 16) * draw_scale`)
remains correct because it scales 2× atlas-space values back to
1× display-space integers. `vita2d_font_text_width()` likewise produces
identical display-space widths because it multiplies the same 2×
advances by the same 0.5 scale.

At draw time, GXM samples the atlas with `draw_scale = 0.5`, so each
output pixel corresponds to a 2×2 source-texel region. LINEAR min
performs a 4-tap bilinear average over that region — a proper 2:1
minification — preserving FreeType's already-antialiased grayscale
edges and further smoothing them.

---

## Why supersampling fixes both prior failure modes

At `draw_scale = 0.5` the bilinear sample point lands **halfway between
source texels by construction**. The texel-edge UV problem that broke
LINEAR at 1:1 mapping (vita2d's sprite vertex path emits texel-edge UVs
without a half-texel center offset, so the bilinear sampler averaged
four neighbouring texels equally → uniform blur) does not apply in the
minification regime: the sample point is supposed to be between texels,
because it represents an output pixel whose footprint covers multiple
source texels.

FreeType's per-pixel grayscale antialiasing
(`FT_LOAD_TARGET_NORMAL`) reaches the framebuffer further smoothed by
the 2:1 bilinear downsample. This fixes the visible stair-step that the
`POINT/POINT` workaround exposed on every text size from the "Menu"
chip (~14 pt) up through 28 pt headers.

---

## What we tried first

| Attempt | Approach | Outcome |
|---------|----------|---------|
| LINEAR/LINEAR (07d354e) | Both filters LINEAR, 1× atlas | Uniform blur — bilinear at texel-edge UVs averages 4 neighbours equally because vita2d's sprite path lacks a half-texel UV center offset. |
| POINT/LINEAR (56b4436, upstream default) | Min POINT, Mag LINEAR, 1× atlas | Blur + horizontal stripes — at 1:1 mapping GXM's choice between min and mag is implementation-defined and flips between scanlines (derivative wobble). |
| POINT/POINT (v0.1.736) | Both filters POINT, 1× atlas | Crisp but stair-stepped at every size — POINT preserves FreeType's per-pixel AA exactly, with no further smoothing past the FreeType pixel grid. |

The recurring pattern across the first two attempts is that any LINEAR
sampling at exactly `draw_scale = 1.0` is in a degenerate regime for
vita2d's UV math. Supersampling guarantees `draw_scale < 1.0`, which
moves the sampler into a regime where LINEAR is well-defined.

---

## Risk if the supersample precondition breaks

The active patch only behaves correctly when every drawn glyph has
`draw_scale ≤ 1.0` — i.e. the requested point size is one of the
prewarmed sizes in `vita/src/ui/ui_text.c`'s `UI_FONT_PREWARM_SIZES`
table. If a future change requests a point size that has no prewarmed
slot, `data.glyph_size` would either be missing or smaller than `size`,
producing `draw_scale > 1.0` and re-entering the texel-edge bilinear
**magnification** regime that produced the original blur.

Any new pt size **must** be added to `UI_FONT_PREWARM_SIZES` (and the
adjacent `_Static_assert` literal and `size_index()` switch) so the
atlas always carries a 2× pre-rendered version of every glyph drawn.

---

## Atlas memory

| Resource | Before | After |
|----------|--------|-------|
| Atlas texture | 512×512 R8 = 256 KB | 1024×1024 R8 = 1 MB |
| Largest glyph | 40 px tall | 80 px tall |
| Prewarm time | baseline | ~4× FreeType raster work, one-time at startup |

1 MB on a 256 MB-of-RAM Vita is acceptable. Prewarm runs once during
the splash phase, well before the user sees the first frame.

---

## ui_text.c — whole-string drawing (not a libvita2d patch)

`vita/src/ui/ui_text.c` routes all text draws through
`vita2d_font_draw_text` (whole-string) rather than per-glyph calls.
This preserves vita2d's internal kerning pairs, which were lost in the
`dee6831` per-glyph workaround. This is a VitaRPS5 policy change; it
does not touch libvita2d.

---

## Build wiring (vita/CMakeLists.txt)

Both `vita2d_font.c` and `texture_atlas.c` are compiled directly into
`VitaRPS5.elf` via `target_sources()`. The linker resolves their symbols
from these direct object files before consulting the `-lvita2d` archive,
so the patched versions shadow the originals without removing
`-lvita2d` (which still provides every other vita2d symbol).

Include path `third-party/libvita2d/include` is added via
`target_include_directories()` so the private headers
(`texture_atlas.h`, `bin_packing_2d.h`, `int_htab.h`, `utils.h`,
`shared.h`) are found during compilation.

---

## Files in this vendor directory

| File | Source | Purpose |
|------|--------|---------|
| `vita2d_font.c` | xerpi/libvita2d master | FreeType font rendering; **VitaRPS5 patch:** 2× supersampled atlas (`VITARPS5_FONT_SUPERSAMPLE`), 1024×1024 atlas |
| `texture_atlas.c` | xerpi/libvita2d master | Glyph atlas; **VitaRPS5 patch:** filters set to LINEAR/LINEAR (works because supersample guarantees draw_scale = 0.5) |
| `include/texture_atlas.h` | xerpi/libvita2d master | Private struct/API for texture_atlas |
| `include/bin_packing_2d.h` | xerpi/libvita2d master | 2D bin packing used by atlas |
| `include/int_htab.h` | xerpi/libvita2d master | Hash table used by atlas |
| `include/utils.h` | xerpi/libvita2d master | utf8_to_ucs2 and GPU utils declarations |
| `include/shared.h` | xerpi/libvita2d master | GXM context externs used by font renderer |
