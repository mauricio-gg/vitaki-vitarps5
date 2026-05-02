# VitaRPS5 Patches to libvita2d

Vendored from xerpi/libvita2d master branch.
Only `vita2d_font.c` and `texture_atlas.c` are vendored; all other
libvita2d symbols come from the system `-lvita2d`.

---

## Current patch state

No active filter patches. Both `vita2d_font.c` and `texture_atlas.c`
use upstream defaults. They are vendored to allow future surgical
patches without forking the entire library.

---

## Experiment record: LINEAR atlas filter (reverted)

### What was tried

`texture_atlas.c` — `texture_atlas_create()` — changed
`vita2d_texture_set_filters()` from upstream `POINT min / LINEAR mag`
to `LINEAR min / LINEAR mag`.

Hypothesis: POINT sampling of the FreeType glyph atlas was causing
stripe artifacts, jagged edges, and baseline drift on header text.
Bilinear filtering was expected to smooth sub-pixel placement.

### What happened

On-device testing confirmed the LINEAR filter made all header text
**visibly blurry**. Smaller body text was similarly affected.

### Root cause

vita2d's sprite vertex path (`vita2d_draw_texture_tint_part_scale`)
does not add the half-texel UV offset required for bilinear sampling to
land on texel centers. For integer-positioned glyph quads, the GXM
sampler ends up at texel-edge UV coordinates, averaging four neighbouring
texels equally — producing the observed blur.

POINT (nearest-neighbour) is robust to this because it returns a single
texel regardless of the fractional UV position. FreeType's own 8-bit
greyscale antialiasing (`FT_LOAD_TARGET_NORMAL`) is sufficient and is
preserved when POINT is used.

### Revert

Both filters restored to upstream defaults in commit that follows
`07d354e`.

---

## ui_text.c: whole-string drawing (not a libvita2d patch)

`vita/src/ui/ui_text.c` routes all text draws through
`vita2d_font_draw_text` (whole-string) rather than per-glyph calls.
This restores vita2d's internal kerning pairs, which were lost in the
`dee6831` per-glyph workaround. This is a VitaRPS5 policy change; it
does not touch libvita2d.

---

## Build wiring (vita/CMakeLists.txt)

Both `vita2d_font.c` and `texture_atlas.c` are compiled directly into
`VitaRPS5.elf` via `target_sources()`. The linker resolves their symbols
from these direct object files before consulting the `-lvita2d` archive,
so any future patches shadow the originals without removing `-lvita2d`
(which still provides all other vita2d symbols).

Include path `third-party/libvita2d/include` is added via
`target_include_directories()` so the private headers
(`texture_atlas.h`, `bin_packing_2d.h`, `int_htab.h`, `utils.h`,
`shared.h`) are found during compilation.

---

## Files in this vendor directory

| File | Source | Purpose |
|------|--------|---------|
| `vita2d_font.c` | xerpi/libvita2d master | FreeType font rendering (no active VitaRPS5 patch) |
| `texture_atlas.c` | xerpi/libvita2d master | Glyph atlas (no active VitaRPS5 patch; upstream POINT/LINEAR preserved) |
| `include/texture_atlas.h` | xerpi/libvita2d master | Private struct/API for texture_atlas |
| `include/bin_packing_2d.h` | xerpi/libvita2d master | 2D bin packing used by atlas |
| `include/int_htab.h` | xerpi/libvita2d master | Hash table used by atlas |
| `include/utils.h` | xerpi/libvita2d master | utf8_to_ucs2 and GPU utils declarations |
| `include/shared.h` | xerpi/libvita2d master | GXM context externs used by font renderer |
