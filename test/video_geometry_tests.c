// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native test for the decoder texture sizing and aspect-fit math in vita/include/video_geometry.h
// (#456 spike). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   ./tools/native_tests.sh   (or: ctest -R video_geometry in build-native-tests)

#include <assert.h>
#include <stdio.h>

#include "video_geometry.h"

/* catches: the 540p path changing. 960x540 must stay a 960x544 texture drawn 1:1, centred 2 rows
 * down, exactly as before the texture followed the stream size. */
static void test_540p_unchanged(void) {
  VideoGeometry g = video_geometry_compute(960, 540);
  assert(g.texture_width == 960 && g.texture_height == 544);
  assert(g.source_width == 960 && g.source_height == 540);
  assert(g.dest_width == 960.0f && g.dest_height == 540.0f);
  assert(g.origin_x == 0.0f && g.origin_y == 2.0f);
}

/* catches: a 720p texture too small for the decoder's frame (it writes 1280x720, so a screen
 * sized texture would be overrun) or a 720p picture not fitted to 960x540. */
static void test_720p(void) {
  VideoGeometry g = video_geometry_compute(1280, 720);
  assert(g.texture_width == 1280 && g.texture_height == 720);
  assert(g.source_width == 1280 && g.source_height == 720);
  assert(g.dest_width == 960.0f && g.dest_height == 540.0f);
  assert(g.origin_x == 0.0f && g.origin_y == 2.0f);
}

/* catches: 1080p drawing the decoder's padding rows (texture is 1088 tall, only 1080 are picture)
 * or a texture of 1080 rows when the decoder is created at 1088. */
static void test_1080p(void) {
  VideoGeometry g = video_geometry_compute(1920, 1080);
  assert(g.texture_width == 1920 && g.texture_height == 1088);
  assert(g.source_width == 1920 && g.source_height == 1080);
  assert(g.dest_width == 960.0f && g.dest_height == 540.0f);
  assert(g.origin_x == 0.0f && g.origin_y == 2.0f);
}

int main(void) {
  test_540p_unchanged();
  test_720p();
  test_1080p();
  printf("video_geometry_tests passed\n");
  return 0;
}
