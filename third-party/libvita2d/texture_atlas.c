/*
 * texture_atlas.c — vendored from xerpi/libvita2d (master branch)
 *
 * VitaRPS5 patches:
 *  - Atlas filters set to LINEAR/LINEAR. Combined with the 2x
 *    supersampled atlas in vita2d_font.c (see VITARPS5_FONT_SUPERSAMPLE),
 *    every drawn glyph performs a legitimate 2:1 bilinear minification:
 *    proper antialiasing without the texel-edge sampling problem that
 *    bit earlier attempts.
 *  - Every glyph is separated from every other glyph, and from the
 *    atlas edges, by at least ATLAS_GLYPH_GAP zeroed texels, so LINEAR
 *    sampling at a glyph edge never reads a neighbouring glyph.
 *  - The hash table starts at ATLAS_HTAB_INITIAL_SIZE entries.
 *
 * See third-party/libvita2d/VITARPS5_PATCHES.md for full rationale.
 */

#include <stdlib.h>
#include <string.h>
#include "texture_atlas.h"

/*
 * Zeroed texels kept between every pair of glyphs and between every glyph
 * and the top/left/right/bottom atlas edge. The atlas texture is zeroed when
 * it is created (vita2d_create_empty_texture_format memsets it), and only
 * the glyph bitmap is ever written, so the gap stays zero. LINEAR sampling
 * reads at most one texel past a glyph edge; 2 leaves margin for the
 * half-texel offsets of the 2:1 minification.
 */
#define ATLAS_GLYPH_GAP 2

/*
 * Initial hash table size. Must be a power of two (int_htab masks the hash
 * with size - 1). int_htab_insert doubles the table when it passes
 * INT_HTAB_MAX_LOAD, so this is a starting point, not a limit.
 */
#define ATLAS_HTAB_INITIAL_SIZE 512

/**
 * texture_atlas_create() - Allocate a glyph atlas backed by a vita2d texture.
 * @width:  Atlas texture width in pixels.
 * @height: Atlas texture height in pixels.
 * @format: GXM texture format (e.g. SCE_GXM_TEXTURE_FORMAT_U8_R111).
 *
 * Returns a newly-allocated texture_atlas on success, NULL on failure.
 *
 * VitaRPS5 patch: both filters set to LINEAR. The active patch in
 * vita2d_font.c renders every glyph at 2x its display size (the cache is
 * keyed by size, so each size has its own bake), so draw_scale = 0.5 at
 * draw time and the sampler is doing 2:1 minification. LINEAR min then averages 4 source texels per output
 * pixel — proper antialiasing — and the texel-edge UV problem that
 * made LINEAR fail at 1:1 mapping does not apply because the sample
 * point lands halfway between source texels by construction.
 *
 * VitaRPS5 patch: the packer's root rectangle starts at (ATLAS_GLYPH_GAP,
 * ATLAS_GLYPH_GAP), so the top and left atlas edges are always zero
 * texels. The right and bottom edges are covered by the gap that
 * texture_atlas_insert() reserves after every glyph.
 */
texture_atlas *texture_atlas_create(int width, int height, SceGxmTextureFormat format)
{
	texture_atlas *atlas = malloc(sizeof(*atlas));
	if (!atlas)
		return NULL;

	bp2d_rectangle rect;
	rect.x = ATLAS_GLYPH_GAP;
	rect.y = ATLAS_GLYPH_GAP;
	rect.w = width - ATLAS_GLYPH_GAP;
	rect.h = height - ATLAS_GLYPH_GAP;

	atlas->texture = vita2d_create_empty_texture_format(width,
							    height,
							    format);
	if (!atlas->texture) {
		free(atlas);
		return NULL;
	}

	atlas->bp_root = bp2d_create(&rect);
	atlas->htab = int_htab_create(ATLAS_HTAB_INITIAL_SIZE);

	/* Both filters set to LINEAR.
	 * Combined with the 2x supersampled atlas (see
	 * VITARPS5_FONT_SUPERSAMPLE in vita2d_font.c) every drawn glyph is
	 * minified 2:1, so LINEAR min/mag perform a proper 4-tap bilinear
	 * downsample. The texel-edge ambiguity that produced uniform blur
	 * with LINEAR at 1:1 does not apply here because the sample point
	 * lands halfway between source texels by construction. */
	vita2d_texture_set_filters(atlas->texture,
				   SCE_GXM_TEXTURE_FILTER_LINEAR,
				   SCE_GXM_TEXTURE_FILTER_LINEAR);

	return atlas;
}

void texture_atlas_free(texture_atlas *atlas)
{
	vita2d_free_texture(atlas->texture);
	bp2d_free(atlas->bp_root);
	int_htab_free(atlas->htab);
	free(atlas);
}

/**
 * texture_atlas_insert() - Reserve space for a glyph and register it.
 * @atlas:        Target atlas.
 * @character:    Hash key for the glyph (vita2d_font.c passes size + glyph index).
 * @size:         Glyph bitmap size in texels.
 * @data:         Metrics stored with the glyph.
 * @inserted_pos: Receives the top-left texel of the glyph bitmap.
 *
 * VitaRPS5 patch: the packer reserves ATLAS_GLYPH_GAP extra texels to the
 * right and below the bitmap. The stored rectangle is exactly the bitmap, so
 * draw position and size are unchanged; only the reserved space grows.
 *
 * Returns 1 on success, 0 if the atlas is full or an allocation failed.
 */
int texture_atlas_insert(texture_atlas *atlas, unsigned int character,
			 const bp2d_size *size,
			 const texture_atlas_entry_data *data,
			 bp2d_position *inserted_pos)
{
	atlas_htab_entry *entry;
	bp2d_node *new_node;
	const bp2d_size reserved = {
		size->w + ATLAS_GLYPH_GAP,
		size->h + ATLAS_GLYPH_GAP
	};

	if (!bp2d_insert(atlas->bp_root, &reserved, inserted_pos, &new_node))
		return 0;

	entry = malloc(sizeof(*entry));
	if (!entry) {
		bp2d_delete(atlas->bp_root, new_node);
		return 0;
	}

	entry->rect.x = inserted_pos->x;
	entry->rect.y = inserted_pos->y;
	entry->rect.w = size->w;
	entry->rect.h = size->h;
	entry->data = *data;

	if (!int_htab_insert(atlas->htab, character, entry)) {
		bp2d_delete(atlas->bp_root, new_node);
		return 0;
	}

	return 1;
}

int texture_atlas_exists(texture_atlas *atlas, unsigned int character)
{
	return int_htab_find(atlas->htab, character) != NULL;
}

int texture_atlas_get(texture_atlas *atlas, unsigned int character,
		      bp2d_rectangle *rect, texture_atlas_entry_data *data)
{
	atlas_htab_entry *entry = int_htab_find(atlas->htab, character);
	if (!entry)
		return 0;

	*rect = entry->rect;
	*data = entry->data;

	return 1;
}
