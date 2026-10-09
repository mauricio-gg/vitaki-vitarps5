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
`generic_font_draw_text()` evaluates to exactly `0.5` for every glyph,
because the cache is keyed by size (see the size-key patch below).
Before that patch this claim was false: the cache was keyed by glyph
index only, so whichever size drew a glyph first set its bitmap for
every size of that font. Light was baked at 20 (40 px bitmaps), so 28
drew at 0.7 and 40 at 1.0; Regular was baked at 14 (28 px), so 16 and
Regular 20 drew scaled from it. All position math
(`pen_x + bitmap_left * draw_scale`, `pen_y - bitmap_top * draw_scale`,
`(advance_x >> 16) * draw_scale`) scales 2x atlas-space values back to
1x display-space integers, and `vita2d_font_text_width()` multiplies the
same 2x advances by the same 0.5 scale.

At draw time, GXM samples the atlas with `draw_scale = 0.5`, so each
output pixel corresponds to a 2×2 source-texel region. LINEAR min
performs a 4-tap bilinear average over that region — a proper 2:1
minification — preserving FreeType's already-antialiased grayscale
edges and further smoothing them.

---

## Active patch - size-aware cache key, zeroed gap, bigger hash table (#378, #370)

**Files:** `vita2d_font.c` and `texture_atlas.c`.

**1. Size-aware cache key (#378).** `generic_font_draw_text()` looks up
and inserts glyphs under `(size << 16) | glyph_index`
(`glyph_cache_key()`, constants `VITARPS5_GLYPH_KEY_INDEX_BITS` and
`VITARPS5_GLYPH_KEY_FIELD_MASK`). Each size of a font now has its own 2x
bake, so large text is no longer scaled from a smaller or larger bake.
A size or glyph index that does not fit 16 bits is not cached under a
wrong key: it logs `[WARN] vita2d_font: glyph <n> at <size>pt does not
fit the 16-bit cache key, skipped` and the glyph is skipped. Roboto has
far fewer than 65536 glyphs, so this is a guard only. A non-zero size
keeps every key non-zero (key 0 is never used).

**2. Zeroed 2 texel gap (#370).** Glyphs were packed edge to edge and
sampled with LINEAR, so the edge texels read the neighbouring glyph and
drew a thin vertical line beside some glyphs ("Controller" read like
"Controllen"). `texture_atlas_insert()` now reserves
`ATLAS_GLYPH_GAP` (2) extra texels right of and below every bitmap, and
the packer's root rectangle starts at (2, 2), so the top and left atlas
edges are zero too. The stored rectangle is still exactly the bitmap, so
draw position and size do not change. The atlas texture is zeroed once
at creation (`vita2d_create_empty_texture_format` calls `memset` on the
GPU buffer), and only bitmap texels are ever written, so the gap stays
zero. Nothing is cleared per frame.

**3. Hash table starts at 512.** `ATLAS_HTAB_INITIAL_SIZE` (was 256).
`int_htab` is not vendored (it comes from the system `-lvita2d`).
Upstream `int_htab_insert` does grow the table: when
`(used + 1) * 100 / size > INT_HTAB_MAX_LOAD` (70) it calls
`int_htab_resize(htab, 2 * size)`. So 512 is a starting size, not a
limit; it avoids a rehash during the splash bake (297 and 291 entries
are 58% and 57% of 512, under the 70% threshold). The size must stay a
power of two because upstream masks the hash with `size - 1`. Key 0 is
legal upstream (an empty slot has key 0 and a NULL value, so a lookup of
0 returns NULL when absent); the old code stored glyph index 0 under key
0, and the new keys are never 0.

**Measured natively (real Roboto fonts, real prewarm charset, upstream
bin packer, 1024 x 1024 atlas).**

| | Regular (14, 16, 20, 20 again) | Light (20, 28, 40) |
|---|---|---|
| Glyphs baked, before | 97 | 97 |
| Glyphs baked, after | 297 | 291 |
| Atlas filled, before | 2.1% | 4.2% |
| Atlas filled, after (bitmap plus gap) | 12.2% | 31.6% |
| Insert failures | 0 | 0 |
| Key collisions | 0 | 0 |
| Glyphs with a non-zero texel within 2 texels of the bitmap, before | 96 of 97 | 96 of 97 |
| Same, after | 0 | 0 |

The Regular count includes three probe glyphs ("Ag|") baked at 20, 28
and 40 by `compute_metrics_for_face()`, which measures every face in the
Regular font.

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

Every glyph is baked at `size * VITARPS5_FONT_SUPERSAMPLE` under its own
size key, so `draw_scale` is `1 / VITARPS5_FONT_SUPERSAMPLE` (0.5) for
any size the UI draws, prewarmed or not. A size missing from the prewarm
no longer changes how it looks, it only bakes its glyphs on first use
(a one-off hitch in a frame instead of on the splash). The cost of a new
size is real, though: it adds a full charset of 2x bitmaps to that
weight's atlas (about 100 glyphs; roughly 2% of 1024 x 1024 at size 14
and 18% at size 40, since the area grows with the square of the size). Add new sizes to `ui_text.c`'s face table so
they are baked during the splash, and check the atlas still fits
(Light is at 31.6%).

---

## Atlas memory

| Resource | Before | After |
|----------|--------|-------|
| Atlas texture | 512x512 R8 = 256 KB | 1024x1024 R8 = 1 MB per font (Regular and Light: 2 MB), unchanged by the size key |
| Largest glyph | 40 px tall | 80 px tall |
| Prewarm work | baseline | 588 glyphs rasterised (Regular 297, Light 291) against 194 with the index-only key, about 3.2 times the FreeType work in a native run, spent once during the splash |

2 MB on a 256 MB-of-RAM Vita is acceptable. Prewarm runs once during
the splash phase; Home starts after it ends.

----------|--------|-------|
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
| `vita2d_font.c` | xerpi/libvita2d master | FreeType font rendering; **VitaRPS5 patches:** 2× supersampled atlas (`VITARPS5_FONT_SUPERSAMPLE`), 1024×1024 atlas, glyph cache keyed by size and glyph index |
| `texture_atlas.c` | xerpi/libvita2d master | Glyph atlas; **VitaRPS5 patches:** filters set to LINEAR/LINEAR (works because the size key and supersample guarantee draw_scale = 0.5), 2 texel zeroed gap around every glyph, hash table starts at 512 |
| `include/texture_atlas.h` | xerpi/libvita2d master | Private struct/API for texture_atlas |
| `include/bin_packing_2d.h` | xerpi/libvita2d master | 2D bin packing used by atlas |
| `include/int_htab.h` | xerpi/libvita2d master | Hash table used by atlas |
| `include/utils.h` | xerpi/libvita2d master | utf8_to_ucs2 and GPU utils declarations |
| `include/shared.h` | xerpi/libvita2d master | GXM context externs used by font renderer |
