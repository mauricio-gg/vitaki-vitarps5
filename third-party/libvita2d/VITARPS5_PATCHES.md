# VitaRPS5 Patches to libvita2d

Vendored from xerpi/libvita2d master branch.
Only `vita2d_font.c` and `texture_atlas.c` are vendored; all other
libvita2d symbols come from the system `-lvita2d`.

---

## Problem

vita2d's FreeType font backend creates its glyph atlas texture via
`vita2d_create_empty_texture_format()` inside `texture_atlas_create()`.
Upstream then calls `vita2d_texture_set_filters(atlas->texture, POINT, LINEAR)`.
The POINT minification filter causes:
- Stripe artifacts on text rendered at sub-integer scale factors
- Jagged edges and baseline drift on all headers / UI labels
- Inconsistency with PNG textures which already use LINEAR/LINEAR (see
  `vita/include/ui/ui_internal.h:136`, `ui_load_png_linear()`)

---

## Patch 1 — texture_atlas.c: LINEAR minification filter

**File:** `third-party/libvita2d/texture_atlas.c`
**Function:** `texture_atlas_create()` (line ~50 in the patched file)

**Original (upstream):**
```c
vita2d_texture_set_filters(atlas->texture,
                           SCE_GXM_TEXTURE_FILTER_POINT,
                           SCE_GXM_TEXTURE_FILTER_LINEAR);
```

**Patched:**
```c
vita2d_texture_set_filters(atlas->texture,
                           SCE_GXM_TEXTURE_FILTER_LINEAR,
                           SCE_GXM_TEXTURE_FILTER_LINEAR);
```

**Why this covers all cases:** The `texture_atlas` uses a fixed-size bin
packing tree (512x512). There is no atlas-grow path; when the atlas is full,
`texture_atlas_insert()` returns 0 and the glyph is skipped. The single
`texture_atlas_create()` allocation is the only site where the filter is set.

---

## Patch 2 — vita2d_font.c: added `vita2d_font_set_atlas_filters()` helper

**File:** `third-party/libvita2d/vita2d_font.c`
**New function added at end of file (after `vita2d_font_text_height`):**

```c
void vita2d_font_set_atlas_filters(vita2d_font *font,
                                   SceGxmTextureFilter min_filter,
                                   SceGxmTextureFilter mag_filter);
```

Allows call-site override of the atlas texture filter after font load.
Under normal usage this is unnecessary because `texture_atlas_create()`
already defaults both filters to LINEAR. The helper exists for diagnostic
use or for callers that need POINT filtering (pixel-perfect bitmap fonts).

**Declaration note:** This function is not declared in any public vita2d.h
header. To call it from `vita/src/ui.c`, add a forward declaration at the
top of `ui.c`:
```c
/* Forward-declare VitaRPS5 patched helper from third-party/libvita2d */
extern void vita2d_font_set_atlas_filters(vita2d_font *font,
                                          SceGxmTextureFilter min_filter,
                                          SceGxmTextureFilter mag_filter);
```

---

## Build wiring (vita/CMakeLists.txt)

Both `vita2d_font.c` and `texture_atlas.c` are compiled directly into
`VitaRPS5.elf` via `target_sources()`. The linker resolves their symbols
from these direct object files before consulting the `-lvita2d` archive,
so the patched versions shadow the originals without removing `-lvita2d`
(which still provides all other vita2d symbols).

Include path `third-party/libvita2d/include` is added via
`target_include_directories()` so the private headers
(`texture_atlas.h`, `bin_packing_2d.h`, `int_htab.h`, `utils.h`,
`shared.h`) are found during compilation.

---

## Files in this vendor directory

| File | Source | Purpose |
|------|--------|---------|
| `vita2d_font.c` | xerpi/libvita2d master + VitaRPS5 patch | FreeType font rendering; adds helper |
| `texture_atlas.c` | xerpi/libvita2d master + VitaRPS5 patch | Glyph atlas; PRIMARY PATCH SITE |
| `include/texture_atlas.h` | xerpi/libvita2d master | Private struct/API for texture_atlas |
| `include/bin_packing_2d.h` | xerpi/libvita2d master | 2D bin packing used by atlas |
| `include/int_htab.h` | xerpi/libvita2d master | Hash table used by atlas |
| `include/utils.h` | xerpi/libvita2d master | utf8_to_ucs2 and GPU utils declarations |
| `include/shared.h` | xerpi/libvita2d master | GXM context externs used by font renderer |
