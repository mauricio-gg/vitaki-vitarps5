# GH #251 — Submit-Through-Loss (kill the ~1 s IDR freeze) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. All C edits go through the `senior-c-architect` agent; every task is reviewed by `code-guardian` before commit.

**Goal:** Replace the ~1 s frozen picture during every IDR round-trip with continuous (briefly artifacted) motion, by submitting missing-reference P-frames to the Vita hardware decoder instead of discarding them — behind a default-OFF config toggle, gated on an SPS safety check.

**Architecture:** Three diagnostic additions (SPS field parse, decode-anomaly counters, IDR-arrival discriminator) land first and ship in the same hardware build as the behavior change, which is a policy flip at the single discard gate in `chiaki_video_receiver_flush_frame()`. The toggle plumbs vita config → `ChiakiConnectInfo` → `session->connect_info`, mirroring `send_actual_start_bitrate` exactly. The IDR request path is left untouched as the backstop, exactly like Moonlight's consecutive-drop IDR fallback.

**Tech Stack:** C (VitaSDK via Docker), Chiaki lib, nanopb/Takion, `sceAvcdec` HW decoder, native `cc` for pure-helper tests.

## Why this approach (research synthesis, 2026-08-14)

- **Slice-header rewriting is dead.** All 112 hardware-observed P-slices carry no `ref_pic_list_modification` (`slice.reference_frame = 0`), so redirecting a reference means *inserting* ≥7 bits of Exp-Golomb syntax mid-header and bit-shifting the whole ~12 KB slice (plus redoing emulation-prevention bytes) on the recv thread. Upstream chiaki-ng has the identical `return false` stub for H.264 — nobody has solved it.
- **Rewriting is also unnecessary.** The lost frame was never submitted, so the decoder's DPB still holds the last good frame as its newest entry. A P-slice with the default (unmodified) reference list resolves index 0 to *whatever is newest in the DPB* — i.e., submitting the frame as-is already predicts from the last good frame. Same effect as the rewrite, zero bitstream surgery.
- **The Vita decoder tolerates this.** vita-moonlight #205 documents this exact hardware decoding imperfect H.264: artifacts but continuous motion with RFI on; "freezes … around 1 second, audio and inputs still real-time" with it off. Across 22 hardware logs of this project, `sceAvcdecDecode` has **never** returned an error; the "fragile DPB" lore traces to DPB *under-provisioning* (a different failure) and to a comment describing behavior that never executed.
- **The one real hazard is `gaps_in_frame_num_value_allowed_flag`.** If the SPS sets it, a spec-conforming decoder synthesizes phantom "non-existing" frames for `frame_num` gaps and the P-frame predicts from garbage. If it's 0 (and we verify this in Task 1 before the toggle can activate), the submitted frame lands in the benign "wrong reference → drift" case by construction.
- **No protocol alternative exists.** Takion has no RFI verb — `CORRUPTFRAME` is a bare frame range with undocumented console-side semantics; `IDRREQUEST` is payload-less; the launch spec has zero error-resilience knobs. Official Sony clients reportedly freeze ~1 s the same way, so the console's IDR latency is treated as irreducible.

## Global Constraints

- Branch: create `fix/251-submit-through-loss` off `diag/idr-roundtrip-251` (keeps PR #256's `PIPE/DELIVERY` logging available for the A/B). Never commit to `main`.
- Build only via `./tools/build.sh` (Docker). Hardware builds: `./tools/build.sh --env testing`. Run `./tools/build.sh format` before every commit with C changes.
- `./tools/build.sh test` only cross-compiles (GH #222) — it never executes. Every pure-helper test in this plan must also be compiled and run natively with `cc`.
- All log analysis on `*_vitarps5-testing.log` files MUST use `grep -a` — BSD grep classifies these logs as binary and silently returns nothing otherwise.
- New literals become named constants (`#define`), no magic numbers.
- The behavior toggle defaults **OFF** in code and in serialized config.
- Commit messages explain *why*; include `Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>`.
- Do not touch the IDR request/cooldown machinery (`IDR_REQUEST_COOLDOWN_MS`/`IDR_REQUEST_TIMEOUT_MS`) or `CASCADE_SKIP_THRESHOLD` values.
- Trust the `DEAD-CODE` annotations on this branch (`grep -rn DEAD-CODE lib vita`); do not "fix" annotated paths as part of this work.

---

### Task 1: Parse `max_num_ref_frames` + `gaps_in_frame_num_value_allowed_flag` from the H.264 SPS

The safety gate for the whole plan. `header_h264()` in `lib/src/bitstream.c:26-82` currently stops right after `log2_max_frame_num_minus4` (line 74) — the two fields we need are next in SPS syntax order, separated only by the `pic_order_cnt_type` block.

**Files:**
- Modify: `lib/include/chiaki/bitstream.h` (the `h264.sps` struct that already holds `log2_max_frame_num_minus4`)
- Modify: `lib/src/bitstream.c:74-81`
- Modify: `lib/src/videoreceiver.c` (~line 435, where the profile header is parsed on profile switch) — one log line
- Create: `test/bitstream_sps_tests.c`
- Modify: `test/CMakeLists.txt` (mirror an existing entry, e.g. `config_vita_tests.c`)

**Interfaces:**
- Produces: two new fields on `ChiakiBitstream`: `bitstream->h264.sps.max_num_ref_frames` (unsigned) and `bitstream->h264.sps.gaps_in_frame_num_value_allowed_flag` (unsigned, 0/1), plus `bitstream->h264.sps.valid_ext` (bool, true only when the extended fields parsed successfully). Task 4 reads all three via `chiaki_bitstream_h264_drift_safe()` (defined here).

- [ ] **Step 1: Add the struct fields and safety-predicate declaration**

In `lib/include/chiaki/bitstream.h`, extend the existing H.264 SPS struct (same struct that holds `log2_max_frame_num_minus4`):

```c
unsigned max_num_ref_frames;
unsigned gaps_in_frame_num_value_allowed_flag;
bool valid_ext; // true when the fields above were parsed successfully
```

and declare next to the other exports:

```c
/* True only when the active H.264 SPS was fully parsed and permits the
 * submit-through-loss policy: a frame_num gap must NOT trigger spec-mandated
 * "non-existing frame" synthesis (clause 8.2.5.2), i.e.
 * gaps_in_frame_num_value_allowed_flag == 0. H.265 and unparsed SPS -> false. */
CHIAKI_EXPORT bool chiaki_bitstream_h264_drift_safe(ChiakiBitstream *bitstream);
```

- [ ] **Step 2: Write the failing native test**

Create `test/bitstream_sps_tests.c`. It builds SPS RBSP bytes with a minimal bit-writer (baseline profile 66, so the high-profile block at `bitstream.c:57-72` is skipped), prepends the 4-byte startcode, and asserts the new fields:

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Minimal MSB-first bit writer for crafting SPS test vectors.
typedef struct { uint8_t buf[64]; unsigned bitpos; } BitWriter;
static void bw_u(BitWriter *bw, uint32_t val, unsigned bits)
{
	for(int i = (int)bits - 1; i >= 0; i--)
	{
		if((val >> i) & 1)
			bw->buf[bw->bitpos / 8] |= (uint8_t)(0x80 >> (bw->bitpos % 8));
		bw->bitpos++;
	}
}
static void bw_ue(BitWriter *bw, uint32_t val)
{
	unsigned len = 0;
	uint32_t v = val + 1;
	while((v >> len) > 1) len++;
	bw_u(bw, 0, len);       // leading zeros
	bw_u(bw, v, len + 1);   // codeword
}

// Emits: startcode + NAL header (type 7) + SPS through the gaps flag.
static unsigned make_sps(uint8_t *out, unsigned max_num_ref_frames, unsigned gaps_flag)
{
	BitWriter bw; memset(&bw, 0, sizeof(bw));
	bw_u(&bw, 0x67, 8);  // forbidden_zero(0) + nal_ref_idc(3) + nal_unit_type(7)
	bw_u(&bw, 66, 8);    // profile_idc = baseline (skips high-profile block)
	bw_u(&bw, 0, 8);     // constraint flags + reserved
	bw_u(&bw, 31, 8);    // level_idc
	bw_ue(&bw, 0);       // seq_parameter_set_id
	bw_ue(&bw, 4);       // log2_max_frame_num_minus4
	bw_ue(&bw, 0);       // pic_order_cnt_type = 0
	bw_ue(&bw, 4);       //   log2_max_pic_order_cnt_lsb_minus4
	bw_ue(&bw, max_num_ref_frames);
	bw_u(&bw, gaps_flag, 1);
	bw_u(&bw, 1, 1);     // rbsp_stop_one_bit (padding)
	while(bw.bitpos % 8) bw_u(&bw, 0, 1);
	out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
	memcpy(out + 4, bw.buf, bw.bitpos / 8);
	return 4 + bw.bitpos / 8;
}

int main(void)
{
	uint8_t sps[80];
	ChiakiBitstream bs;
	memset(&bs, 0, sizeof(bs));
	bs.codec = CHIAKI_CODEC_H264;

	unsigned n = make_sps(sps, 8, 0);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(bs.h264.sps.valid_ext);
	assert(bs.h264.sps.max_num_ref_frames == 8);
	assert(bs.h264.sps.gaps_in_frame_num_value_allowed_flag == 0);
	assert(chiaki_bitstream_h264_drift_safe(&bs));

	n = make_sps(sps, 4, 1);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(bs.h264.sps.gaps_in_frame_num_value_allowed_flag == 1);
	assert(!chiaki_bitstream_h264_drift_safe(&bs));

	// pic_order_cnt_type == 1 exercises the conditional-field skip path
	// (make a second builder variant if header_h264 gains that branch).

	memset(&bs, 0, sizeof(bs));
	bs.codec = CHIAKI_CODEC_H264;
	assert(!chiaki_bitstream_h264_drift_safe(&bs)); // unparsed -> unsafe

	printf("bitstream_sps_tests: all assertions passed\n");
	return 0;
}
```

Include `bitstream.c` the way the existing tests in `test/` include their units (check `test/config_vita_tests.c` + `test/stubs/` for the stub pattern for `CHIAKI_LOGW`/`ChiakiLog`; add a stub only if one doesn't exist).

- [ ] **Step 3: Run the test natively to verify it fails**

```bash
cc -std=c99 -I lib/include -I lib/src -I test/stubs test/bitstream_sps_tests.c -o /tmp/bitstream_sps_tests && /tmp/bitstream_sps_tests
```

(Adjust `-I`/stub flags to match how the existing native reruns of `test/` files are done.) Expected: compile error (fields don't exist yet) or assertion failure.

- [ ] **Step 4: Implement the parser extension**

In `lib/src/bitstream.c`, replace the `return true;` at line 81 (keep the `log2_max_frame_num_minus4` validation above it):

```c
	bitstream->h264.sps.valid_ext = false;

	unsigned pic_order_cnt_type = vl_rbsp_ue(&rbsp);
	if(pic_order_cnt_type == 0)
	{
		vl_rbsp_ue(&rbsp); // log2_max_pic_order_cnt_lsb_minus4
	}
	else if(pic_order_cnt_type == 1)
	{
		vl_rbsp_u(&rbsp, 1); // delta_pic_order_always_zero_flag
		vl_rbsp_ue(&rbsp);   // offset_for_non_ref_pic (se: same bit length as ue)
		vl_rbsp_ue(&rbsp);   // offset_for_top_to_bottom_field (se)
		unsigned cycle = vl_rbsp_ue(&rbsp); // num_ref_frames_in_pic_order_cnt_cycle
		if(cycle > 255)
			return true; // implausible; keep base fields, skip ext
		for(unsigned i = 0; i < cycle; i++)
			vl_rbsp_ue(&rbsp); // offset_for_ref_frame[i] (se)
	}
	// pic_order_cnt_type == 2: no additional fields

	bitstream->h264.sps.max_num_ref_frames = vl_rbsp_ue(&rbsp);
	bitstream->h264.sps.gaps_in_frame_num_value_allowed_flag = vl_rbsp_u(&rbsp, 1);
	bitstream->h264.sps.valid_ext = true;

	return true;
```

Notes for the implementer: `se(v)` and `ue(v)` occupy identical bit counts, so `vl_rbsp_ue` is a correct *skip* for signed fields (use `vl_rbsp_se` instead if `vl_rbsp.h` provides it — check first). Parse failures past line 74 must degrade to `valid_ext = false`, never to `return false` — the base parse (which the slice parser depends on) already succeeded.

Then add the predicate at the bottom of `bitstream.c` next to the other exported functions:

```c
CHIAKI_EXPORT bool chiaki_bitstream_h264_drift_safe(ChiakiBitstream *bitstream)
{
	if(bitstream->codec != CHIAKI_CODEC_H264)
		return false;
	if(!bitstream->h264.sps.valid_ext)
		return false;
	return bitstream->h264.sps.gaps_in_frame_num_value_allowed_flag == 0;
}
```

- [ ] **Step 5: Log the fields once per profile switch**

In `lib/src/videoreceiver.c`, immediately after the successful `chiaki_bitstream_header(...)` call on profile switch (~line 435), add:

```c
if(video_receiver->bitstream.codec == CHIAKI_CODEC_H264 && video_receiver->bitstream.h264.sps.valid_ext)
	CHIAKI_LOGI(video_receiver->log, "SPS: max_num_ref_frames=%u gaps_in_frame_num_allowed=%u",
		video_receiver->bitstream.h264.sps.max_num_ref_frames,
		video_receiver->bitstream.h264.sps.gaps_in_frame_num_value_allowed_flag);
else if(video_receiver->bitstream.codec == CHIAKI_CODEC_H264)
	CHIAKI_LOGW(video_receiver->log, "SPS: extended fields not parsed (drift-submit will stay disabled)");
```

- [ ] **Step 6: Run the native test to verify it passes**

Same command as Step 3. Expected: `bitstream_sps_tests: all assertions passed`.

- [ ] **Step 7: Cross-compile check + format + commit**

```bash
./tools/build.sh test && ./tools/build.sh format
git add lib/include/chiaki/bitstream.h lib/src/bitstream.c lib/src/videoreceiver.c test/bitstream_sps_tests.c test/CMakeLists.txt
git commit -m "diag(bitstream): parse SPS ref-count and frame_num-gap policy

The submit-through-loss fix for #251 is only safe when the PS5's SPS has
gaps_in_frame_num_value_allowed_flag=0 (wrong-reference drift, not
spec-mandated phantom-frame synthesis). Parse the two fields the old
parser stopped one syntax element short of, and expose the go/no-go
predicate the policy gate will use.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 2: Session-long decode-anomaly counters in the Vita decoder

Today `PIPE/DECODE` logging stops after the first 30 frames (`vita/src/video.c:903-906`) and a decode failure is invisible at the call boundary (`ret < 0` and `numOfOutput != 1` both `return 0` at `video.c:911/:917`). The A/B needs to see, for the whole session, whether drift-submitted frames ever make the decoder error or swallow output.

**Files:**
- Modify: `vita/src/video.c` (decode result handling, ~lines 899-918, and the stream-stats/shutdown summary path)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: log lines only (`DECODE/ANOM` and a totals line); no API surface.

- [ ] **Step 1: Add counters and per-occurrence logging**

In `vita/src/video.c`, next to the existing decode-state statics, add:

```c
// Session-long decode anomaly counters (GH #251 A/B instrumentation).
// Baseline across 22 hardware logs: ret<0 has NEVER occurred; numOfOutput==0
// only in the first ~900ms after stream start. Any deviation under
// drift-submit is the signal these exist to catch.
static uint32_t decode_neg_ret_count = 0;
static uint32_t decode_no_output_count = 0;
```

In the `ret < 0` branch (~`video.c:908-911`) add before the return:

```c
decode_neg_ret_count++;
LOGE("DECODE/ANOM neg_ret=0x%08X count=%u", (unsigned)ret, decode_neg_ret_count);
```

In the `numOfOutput != 1` branch (~`video.c:913-917`) add:

```c
decode_no_output_count++;
LOGD("DECODE/ANOM no_output count=%u bufSize=0x%X", decode_no_output_count, (unsigned)au.es.size);
```

(Use the project's actual logging macros as found in the file — match whatever `PIPE/DECODE` uses; do not use `printf`.)

- [ ] **Step 2: Emit totals at stream teardown**

In `shutdown_media_pipeline()`'s existing summary/cleanup path (or the existing end-of-stream stats log in `video.c` if one exists — search for the shutdown logging added around `vita_h264_stop()`), add one line and reset both counters to 0 for the next stream:

```c
LOGI("DECODE/ANOM totals neg_ret=%u no_output=%u", decode_neg_ret_count, decode_no_output_count);
decode_neg_ret_count = 0;
decode_no_output_count = 0;
```

- [ ] **Step 3: Build, format, commit**

```bash
./tools/build.sh test && ./tools/build.sh format
git add vita/src/video.c
git commit -m "diag(video): count decoder anomalies for the whole session

PIPE/DECODE goes quiet after 30 frames, and a negative sceAvcdecDecode
return is logged but indistinguishable from success at the call boundary.
The #251 drift-submit A/B needs a full-session record of whether feeding
missing-reference P-frames ever makes the hardware decoder error or
withhold output. Baseline: zero negative returns in 22 logs.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 3: IDR-arrival discriminator (settle "console is slow" vs "keyframe died in the burst")

Rebuilds the salvageable ideas from `docs/ai/wip/251-idr-probe-incomplete.patch` (which does not compile — its constants block was deleted; do **not** apply it). Today only the "recovery complete" branch logs I-slices, so unsolicited keyframes are invisible, and a keyframe lost to FEC in the same burst is indistinguishable from the console never sending one.

**Files:**
- Modify: `lib/src/videoreceiver.c`
- Modify: `lib/include/chiaki/videoreceiver.h` (two struct fields)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: log lines `PIPE/ISLICE ...` and `fec_fail_kf=` in the existing stage-window log; struct fields `fec_fail_kf_count`, `units_expected_ewma` on `ChiakiVideoReceiver`.

- [ ] **Step 1: Log every I-slice arrival**

In `chiaki_video_receiver_flush_frame()` where the slice parse succeeds (`videoreceiver.c:732-736`), add inside the `slice.slice_type == CHIAKI_BITSTREAM_SLICE_I` branch:

```c
uint64_t islice_now_ms = chiaki_time_now_monotonic_ms();
uint64_t idr_age_ms = (video_receiver->idr_request_pending && islice_now_ms >= video_receiver->idr_request_start_ms)
	? islice_now_ms - video_receiver->idr_request_start_ms : 0;
CHIAKI_LOGI(video_receiver->log, "PIPE/ISLICE frame=%d pending=%d age_ms=%llu",
	(int)video_receiver->frame_index_cur,
	video_receiver->idr_request_pending ? 1 : 0,
	(unsigned long long)idr_age_ms);
```

- [ ] **Step 2: Classify FEC-failed frames by size**

Add to `ChiakiVideoReceiver` (in `lib/include/chiaki/videoreceiver.h`, near the other `stage_window_*` fields):

```c
uint32_t fec_fail_kf_count;      // FEC-failed frames whose expected unit count looked keyframe-sized
uint32_t units_expected_ewma_x8; // EWMA of units_source_expected, fixed-point x8
```

At the top of `videoreceiver.c` with the other constants:

```c
// Keyframe-size classifier for FEC-failed frames (GH #251 discriminator).
// A frame whose expected source-unit count exceeds KF_UNITS_MULT x the running
// mean is presumed to be an IDR that died in the loss burst. Heuristic,
// diagnostic-only; thresholds bound the EWMA so startup noise can't classify.
#define VIDEO_IDR_KEYFRAME_UNITS_MULT 3
#define VIDEO_IDR_UNITS_EWMA_MIN 2
#define VIDEO_IDR_UNITS_EWMA_SHIFT 3 // EWMA alpha = 1/8
```

In the flush-result handling: on every **successful** flush (the `succ` path), update the EWMA from the frame processor's `units_source_expected` (field exists at `lib/src/frameprocessor.c:84`; read it via the `frame_processor` member before it's reset for the next frame):

```c
uint32_t units = video_receiver->frame_processor.units_source_expected;
if(units > 0)
{
	if(video_receiver->units_expected_ewma_x8 == 0)
		video_receiver->units_expected_ewma_x8 = units * 8;
	else
		video_receiver->units_expected_ewma_x8 +=
			((int32_t)(units * 8) - (int32_t)video_receiver->units_expected_ewma_x8) >> VIDEO_IDR_UNITS_EWMA_SHIFT;
}
```

In the `FEC_FAILED` branch (`videoreceiver.c:696-720`), classify:

```c
uint32_t ewma_units = video_receiver->units_expected_ewma_x8 / 8;
uint32_t fail_units = video_receiver->frame_processor.units_source_expected;
if(ewma_units >= VIDEO_IDR_UNITS_EWMA_MIN && fail_units >= ewma_units * VIDEO_IDR_KEYFRAME_UNITS_MULT)
{
	video_receiver->fec_fail_kf_count++;
	CHIAKI_LOGW(video_receiver->log, "PIPE/FEC_FAIL_KF frame=%d units=%u ewma=%u",
		(int)video_receiver->frame_index_cur, fail_units, ewma_units);
}
```

(If `units_source_expected` is not readable at these points, expose it with a one-line accessor in `frameprocessor.h` rather than duplicating state.)

- [ ] **Step 3: Add `fec_fail_kf=` to the existing stage-window log line**

Find the periodic window log that prints `stage_window_frames`/`stage_window_drops` (search `stage_window` in `videoreceiver.c`) and append `fec_fail_kf=%u` with `video_receiver->fec_fail_kf_count`, resetting the counter with the other window counters.

- [ ] **Step 4: Build, format, commit**

```bash
./tools/build.sh test && ./tools/build.sh format
git add lib/src/videoreceiver.c lib/include/chiaki/videoreceiver.h
git commit -m "diag(videoreceiver): make unsolicited I-slices and burst-killed IDRs visible

Interpretation key for the #251 A/B: PIPE/ISLICE with pending=0 means the
console volunteered a keyframe; PIPE/FEC_FAIL_KF>0 during an IDR wait means
the console DID answer and the reply died in the same loss burst (delivery
problem), while 0 means the console is genuinely slow to produce one
(round-trip problem). Rebuilt from the non-compiling wip probe patch with
the missing constants defined.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 4: The `submit_on_missing_ref` toggle and policy flip

The behavior change. One decision point moves: `videoreceiver.c:775-789` currently sets `succ = false` for a missing-reference P-frame; with the toggle ON and the Task 1 SPS gate green, the frame is submitted as-is (the decoder's default reference list resolves to the newest DPB entry = last good frame → drift, not blackout). IDR request pressure and console reporting are unchanged.

**Files:**
- Modify: `vita/include/config.h` (~line 70, next to `force_30fps`)
- Modify: `vita/src/config.c` (default ~line 56, parse table ~line 399, serialize ~lines 550/710 — mirror `clamp_soft_restart_bitrate` in all four places)
- Modify: `vita/src/ui/ui_screens.c` (settings toggle — mirror the `clamp_soft_restart_bitrate` toggle at ~:1099 and its row render at ~:1200)
- Modify: `lib/include/chiaki/session.h` (`ChiakiConnectInfo` ~line 86, and the session-internal `connect_info` struct ending at ~line 223)
- Modify: `lib/src/session.c` (~line 282, next to the `send_actual_start_bitrate` copy)
- Modify: `vita/src/host.c` (~line 368, next to the `send_actual_start_bitrate` assignment)
- Modify: `lib/src/videoreceiver.c:775-789`

**Interfaces:**
- Consumes: `chiaki_bitstream_h264_drift_safe(ChiakiBitstream *)` from Task 1.
- Produces: `context.config.submit_on_missing_ref` (bool, default false); `session->connect_info.submit_on_missing_ref`; log line `PIPE/DRIFT_SUBMIT`.

- [ ] **Step 1: Config field, default OFF, parse + serialize + UI**

`vita/include/config.h` (with the other stream bools):

```c
bool submit_on_missing_ref;  // GH #251 A/B: feed missing-ref P-frames to the decoder (drift) instead of freezing for the IDR
```

`vita/src/config.c`: default `cfg->submit_on_missing_ref = false;` (~line 56 block); table entry `{"submit_on_missing_ref", false, &cfg->submit_on_missing_ref},` (~line 399 table); add to the serializer format string and value list (~lines 550/710) exactly as `clamp_soft_restart_bitrate` appears in each. `vita/src/ui/ui_screens.c`: clone the `clamp_soft_restart_bitrate` toggle row (label: `"Motion during loss (artifacts)"`), same interaction pattern, both touch and controller.

- [ ] **Step 2: Plumb through ChiakiConnectInfo → session**

`lib/include/chiaki/session.h`: add `bool submit_on_missing_ref;` to BOTH `ChiakiConnectInfo` (~line 86, next to `send_actual_start_bitrate`) and the session-internal anonymous `connect_info` struct (fields are copied selectively — adding to one is not enough; the struct closes at line ~223).
`lib/src/session.c` (~282): `session->connect_info.submit_on_missing_ref = connect_info->submit_on_missing_ref;`
`vita/src/host.c` (~368): `chiaki_connect_info.submit_on_missing_ref = context.config.submit_on_missing_ref;`

- [ ] **Step 3: The policy flip in `videoreceiver.c`**

Replace the body of the `if(!recovered)` block (`videoreceiver.c:775-789`) with:

```c
if(!recovered)
{
	bool drift_submit = video_receiver->session->connect_info.submit_on_missing_ref
		&& chiaki_bitstream_h264_drift_safe(&video_receiver->bitstream);
	chiaki_stream_connection_report_missing_ref(&video_receiver->session->stream_connection);
	/* Request IDR immediately in both modes; cooldown (IDR_REQUEST_COOLDOWN_MS=100ms)
	 * prevents flooding. Under drift-submit the IDR is the cleanup pass that
	 * erases accumulated drift; without it, it is the only way motion resumes. */
	uint64_t idr_now_ms = chiaki_time_now_monotonic_ms();
	video_receiver_maybe_request_idr(video_receiver, idr_now_ms, "missing_ref");
	if(drift_submit)
	{
		/* Submit the intact frame. The lost reference was never submitted, so
		 * the decoder DPB's newest entry is the last good frame; the default
		 * (unmodified) reference list resolves to it. Wrong reference ->
		 * bounded drift, refreshed by the pending IDR. Do NOT count this frame
		 * lost and do NOT bump the cascade counter: on callback success the
		 * frame becomes a legitimate reference (add_ref_frame below). */
		CHIAKI_LOGW(video_receiver->log, "PIPE/DRIFT_SUBMIT frame=%d missing_ref=%d",
			(int)video_receiver->frame_index_cur, (int)ref_frame_index);
	}
	else
	{
		succ = false;
		video_receiver->frames_lost = saturating_add_u32(video_receiver->frames_lost, 1U);
		video_receiver->consecutive_missing_ref++;
		CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d (cascade=%u)",
			(int)ref_frame_index, (int)video_receiver->frame_index_cur,
			video_receiver->consecutive_missing_ref);
	}
}
```

Semantics preserved deliberately: `report_missing_ref` (diagnostic counter) and the IDR request fire in both modes; `idr_request_pending` still clears only on a real I-slice (`videoreceiver.c:738-746`); the cascade-skip backstop still exists for the OFF path and for drift frames whose callback fails; the CORRUPTFRAME reporting for the genuinely-lost frame is untouched (it happens in the FEC-fail/gap paths before this point). On drift-submit success, the existing `:794-813` path runs unchanged: `add_ref_frame()` legitimizes the frame so the next P-frame chains normally, and `consecutive_missing_ref` resets.

- [ ] **Step 4: Build, format, review, commit**

```bash
./tools/build.sh test && ./tools/build.sh format
git add vita/include/config.h vita/src/config.c vita/src/ui/ui_screens.c \
        lib/include/chiaki/session.h lib/src/session.c vita/src/host.c lib/src/videoreceiver.c
git commit -m "feat(video): optional submit-through-loss for missing-ref P-frames (#251)

A missing-reference P-frame is fully assembled and decodable; discarding
it buys nothing except a frozen picture for the 0.8-1.5s the PS5 takes to
answer an IDR request. With the toggle on (default off) and the SPS
confirming gaps_in_frame_num_value_allowed_flag=0, submit the frame: the
decoder predicts from the last good frame (bounded drift) and the pending
IDR erases the artifacts. Same trade chiaki-ng shipped for HEVC in v1.6.3;
vita-moonlight #205 demonstrates it on this exact decoder.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 5: Hardware A/B validation

**Files:**
- Create: `docs/ai/251-drift-submit-ab-results.md` (results write-up)
- Modify: version bump files produced by the build (commit them — never discard)

**Interfaces:**
- Consumes: everything above, in one build.

- [ ] **Step 1: Build and deploy the testing build**

```bash
./tools/build.sh --env testing
./tools/build.sh deploy <vita_ip>   # confirm device IP with the user first
```

- [ ] **Step 2: Baseline run (toggle OFF)**

Stream for ≥5 minutes on the known-bad network conditions. Confirm in the log (all greps with `-a`):
- `SPS: max_num_ref_frames=... gaps_in_frame_num_allowed=...` — **this is the go/no-go readout**. If `gaps_in_frame_num_allowed=1`, STOP: the drift-safe gate will (correctly) keep the feature inert; report back before proceeding.
- `PIPE/ISLICE` lines present; `DECODE/ANOM totals` at teardown; `fec_fail_kf=` in window lines.

- [ ] **Step 3: A/B run (toggle ON)**

Enable "Motion during loss (artifacts)" in settings, stream the same content/conditions ≥5 minutes. Pull both logs.

- [ ] **Step 4: Analysis**

For each log:

```bash
grep -a "PIPE/DRIFT_SUBMIT\|PIPE/ISLICE\|DECODE/ANOM\|FEC_FAIL_KF\|Requesting IDR\|recovery complete" <log>
```

Success criteria: (a) freeze windows (IDR request → I-slice age) still ~1 s but the screen shows motion — subjective check on device plus `display_fps` continuity through the window in the PIPE/DELIVERY data; (b) `DECODE/ANOM neg_ret` stays 0 and `no_output` stays startup-only; (c) no new UI-thread stalls. Failure criteria: decoder anomalies correlated with `DRIFT_SUBMIT`, or visible corruption persisting AFTER an I-slice (would indicate DPB damage → see Contingencies).
Also record the discriminator answer: during IDR waits, `FEC_FAIL_KF > 0` ⇒ the console answered and the keyframe died in the burst; `== 0` throughout ⇒ the console is genuinely slow.

- [ ] **Step 5: Write up and close the loop**

Write `docs/ai/251-drift-submit-ab-results.md` (symptoms, log excerpts, verdict, whether the toggle should default ON). Comment findings on GH #251. Hand tracker updates (TODO/DONE/PROGRESS) to the `project-tracker` agent. Commit everything including version bumps.

---

## Contingencies and explicitly-rejected paths

- **If the decoder wedges under drift-submit** (persistent corruption after I-slices, or `neg_ret` anomalies): the escape hatch is `_sceAvcdecDecodeFlush` (NID `0x80C78430`), hand-declared exactly like the codec-engine NIDs at `vita/src/video.c:561-570` — a DPB flush without teardown. A full `sceAvcdecDeleteDecoder`/`CreateDecoder` cycle is cheap (the CDRAM memblock and library init survive it) **but requires SPS/PPS re-injection** from `video_receiver->profiles[profile_cur].header` — the header currently reaches the decoder only on profile switch, so a naive reset silently never restarts. Build this only if the A/B demonstrates the need.
- **H.264 slice-header reference rewriting: rejected.** No `ref_pic_list_modification` exists in any observed slice, so rewriting means inserting Exp-Golomb syntax and bit-shifting ~12 KB per frame on the recv thread — and the result (predict from last good frame) is what plain submission already achieves.
- **Protocol-level RFI: rejected.** No such verb exists in Takion; `CORRUPTFRAME` semantics on the console are undocumented anywhere; the launch spec exposes no error-resilience knobs.
- **Follow-up experiments (file as GH issues, not in this plan):** (1) send `ServerSettingsPayload.idr_period` / `VideoCapturePayload.idr_period` — defined in `takion.proto`, never sent by any client; if the PS5 honors a short IDR period, freezes get bounded without a request round-trip, at a bitrate cost. (2) `HEADERREQUEST(VIDEO)` as an SPS/PPS refresh mechanism. (3) `docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md` corrections: ref slots are 8 not 16 (`videoreceiver.h:22`), and its §2 step 7 describes P-frames reaching the decoder with wrong references — which never happened pre-this-plan.
