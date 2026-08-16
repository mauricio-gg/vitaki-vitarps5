// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_BITSTREAM_H
#define CHIAKI_BITSTREAM_H

#include <stdint.h>

#include "common.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_bitstream_t
{
	ChiakiLog *log;
	ChiakiCodec codec;
	union
	{
		struct
		{
			struct
			{
				uint32_t log2_max_frame_num_minus4;
				unsigned max_num_ref_frames;
				unsigned gaps_in_frame_num_value_allowed_flag;
				bool valid_ext; // true when the fields above were parsed successfully
			} sps;
		} h264;

		struct
		{
			struct
			{
				uint32_t log2_max_pic_order_cnt_lsb_minus4;
			} sps;
		} h265;
	};
} ChiakiBitstream;

typedef enum chiaki_bitstream_slice_type_t
{
	CHIAKI_BITSTREAM_SLICE_UNKNOWN = 0,
	CHIAKI_BITSTREAM_SLICE_I,
	CHIAKI_BITSTREAM_SLICE_P,
} ChiakiBitstreamSliceType;

typedef struct chiaki_bitstream_slice_t
{
	ChiakiBitstreamSliceType slice_type;
	unsigned reference_frame;
} ChiakiBitstreamSlice;

CHIAKI_EXPORT void chiaki_bitstream_init(ChiakiBitstream *bitstream, ChiakiLog *log, ChiakiCodec codec);
CHIAKI_EXPORT bool chiaki_bitstream_header(ChiakiBitstream *bitstream, uint8_t *data, unsigned size);
CHIAKI_EXPORT bool chiaki_bitstream_slice(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, ChiakiBitstreamSlice *slice);
CHIAKI_EXPORT bool chiaki_bitstream_slice_set_reference_frame(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, unsigned reference_frame);

/* True only when the active H.264 SPS was fully parsed and permits the
 * submit-through-loss policy: a frame_num gap must NOT trigger spec-mandated
 * "non-existing frame" synthesis (clause 8.2.5.2), i.e.
 * gaps_in_frame_num_value_allowed_flag == 0. H.265 and unparsed SPS -> false. */
CHIAKI_EXPORT bool chiaki_bitstream_h264_drift_safe(ChiakiBitstream *bitstream);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_BITSTREAM_H
