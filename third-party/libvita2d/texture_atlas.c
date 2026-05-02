/*
 * texture_atlas.c — vendored from xerpi/libvita2d (master branch)
 *
 * VitaRPS5 patch: atlas filters set to LINEAR/LINEAR. Combined with
 * the 2x supersampled atlas in vita2d_font.c (see
 * VITARPS5_FONT_SUPERSAMPLE), every drawn glyph performs a
 * legitimate 2:1 bilinear minification — proper antialiasing
 * without the texel-edge sampling problem that bit earlier attempts.
 *
 * See third-party/libvita2d/VITARPS5_PATCHES.md for full rationale.
 */

#include <stdlib.h>
#include <string.h>
#include "texture_atlas.h"

/**
 * texture_atlas_create() - Allocate a glyph atlas backed by a vita2d texture.
 * @width:  Atlas texture width in pixels.
 * @height: Atlas texture height in pixels.
 * @format: GXM texture format (e.g. SCE_GXM_TEXTURE_FORMAT_U8_R111).
 *
 * Returns a newly-allocated texture_atlas on success, NULL on failure.
 *
 * VitaRPS5 patch: both filters set to LINEAR. The active patch in
 * vita2d_font.c renders every glyph at 2x its display size, so
 * draw_scale = 0.5 at draw time and the sampler is doing 2:1
 * minification. LINEAR min then averages 4 source texels per output
 * pixel — proper antialiasing — and the texel-edge UV problem that
 * made LINEAR fail at 1:1 mapping does not apply because the sample
 * point lands halfway between source texels by construction.
 */
texture_atlas *texture_atlas_create(int width, int height, SceGxmTextureFormat format)
{
	texture_atlas *atlas = malloc(sizeof(*atlas));
	if (!atlas)
		return NULL;

	bp2d_rectangle rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = width;
	rect.h = height;

	atlas->texture = vita2d_create_empty_texture_format(width,
							    height,
							    format);
	if (!atlas->texture) {
		free(atlas);
		return NULL;
	}

	atlas->bp_root = bp2d_create(&rect);
	atlas->htab = int_htab_create(256);

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

int texture_atlas_insert(texture_atlas *atlas, unsigned int character,
			 const bp2d_size *size,
			 const texture_atlas_entry_data *data,
			 bp2d_position *inserted_pos)
{
	atlas_htab_entry *entry;
	bp2d_node *new_node;

	if (!bp2d_insert(atlas->bp_root, size, inserted_pos, &new_node))
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
