# Motion Macroblocking Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate motion-triggered macroblocking by fixing the receiver's reference-frame window mismatch with the Vita HW decoder and adding earlier IDR recovery on the first unrecoverable missing reference.

**Architecture:** Two staged changes to `lib/src/videoreceiver.c`, each on its own branch/PR and validated on-device before the next begins. Task 1 (structural): shrink the receiver's reference-frame store from 16 to 8 entries so it matches the Vita HW decoder's 8-frame DPB; this exposes references that the decoder can't satisfy, allowing the recovery path to fire instead of the decoder silently producing macroblocked output. Task 2 (behavioral, done after Task 1 is on-device validated): request an IDR keyframe on the *first* unrecoverable missing reference instead of waiting for three consecutive failures; the existing 100 ms cooldown (`IDR_REQUEST_COOLDOWN_MS`) prevents flooding.

**Tech Stack:** C (ANSI C), VitaSDK, Chiaki lib layer (`lib/`), Docker build toolchain (`./tools/build.sh`).

## Background (read before touching code)

- `docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md` — full root-cause analysis; read this first.
- `docs/ai/FPS_HEALTH_INVESTIGATION.md` — same family of network-driven P-frame collapse.
- The Vita HW H.264 decoder (`vita/include/video.h:35`: `#define REF_FRAMES 8`) retains only 8 decoded frames in its DPB. The lib-layer receiver (`lib/include/chiaki/videoreceiver.h:34`) tracks 16 reference frame slots. When a P-frame references a frame that is in slots 9-16 of the receiver's store (decoded, but evicted from the HW decoder's DPB), the receiver says "valid reference" and passes the frame for decode. The decoder silently produces garbage. The receiver never sees an error. Macroblocking persists until the PS5 eventually sends an I-slice.

## Global Constraints

- ANSI C; no C++ or VitaSDK-only APIs in `lib/`.
- Build and test exclusively via Docker: `./tools/build.sh`, `./tools/build.sh test`, `./tools/build.sh --env testing`.
- Do not modify any file in `third-party/` or `vita/` for Task 1 or Task 2.
- `lib/` must not `#include` any header from `vita/include/` — the Vita-specific `REF_FRAMES` constant must be mirrored independently in the lib layer.
- Each task gets its own branch (`fix/`) and PR. Task 2's branch is cut from main **after Task 1 is merged and on-device validated**.
- Run `./tools/build.sh format` before every commit.
- Every commit message must end with: `Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>`

---

## Task 1: Reduce receiver reference-frame store from 16 to 8 (match Vita HW decoder DPB)

**Files:**
- Modify: `lib/include/chiaki/videoreceiver.h:17` (add new `#define` before struct)
- Modify: `lib/include/chiaki/videoreceiver.h:34` (`reference_frames[16]` → `reference_frames[CHIAKI_VIDEO_RECEIVER_REF_SLOTS]`)
- Modify: `lib/src/videoreceiver.c:26,30,41` (update hardcoded 15/16 in `add_ref_frame` and `have_ref_frame`)
- Modify: `lib/src/videoreceiver.c:467` (update alternate-ref scan upper bound)

**Interfaces:**
- `add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)` — static, internal. Behaviour unchanged externally; now evicts at 8 instead of 16.
- `have_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame) → bool` — static, internal. Now searches 8 slots.
- `video_receiver_apply_cascade_reset` uses `sizeof(video_receiver->reference_frames)` — automatically correct after array resize; **do not change it**.

- [ ] **Step 1: Create feature branch**

```bash
git checkout main && git pull
git checkout -b fix/receiver-ref-slots-8
```

- [ ] **Step 2: Grep for every hardcoded `16` referencing the reference_frames array**

```bash
grep -n "reference_frames\|ref.*16\|16.*ref" \
  lib/include/chiaki/videoreceiver.h \
  lib/src/videoreceiver.c
```

Expected hits:
- `videoreceiver.h:34`: `int32_t reference_frames[16];`
- `videoreceiver.c:26`: `sizeof(int32_t) * 15`
- `videoreceiver.c:30`: `i=15; i>=0`
- `videoreceiver.c:41`: `i<16`
- `videoreceiver.c:467`: `i<16`

No other files should contain hardcoded 16 for this array. If you see any other hits, update them too before proceeding.

- [ ] **Step 3: Add the `CHIAKI_VIDEO_RECEIVER_REF_SLOTS` constant to `videoreceiver.h`**

Open `lib/include/chiaki/videoreceiver.h`. After line 17 (`#define CHIAKI_VIDEO_PROFILES_MAX 8`), add:

```c
// Must match REF_FRAMES in vita/include/video.h (Vita HW H.264 decoder DPB size).
// Receiver window > HW decoder DPB causes silent macroblocking: the receiver
// approves P-frames referencing evicted decoder slots. Keep these in sync.
#define CHIAKI_VIDEO_RECEIVER_REF_SLOTS 8
```

- [ ] **Step 4: Resize the struct field**

In `lib/include/chiaki/videoreceiver.h`, change line 34 from:
```c
	int32_t reference_frames[16];
```
to:
```c
	int32_t reference_frames[CHIAKI_VIDEO_RECEIVER_REF_SLOTS];
```

- [ ] **Step 5: Add local alias in `videoreceiver.c` for brevity**

In `lib/src/videoreceiver.c`, after the `#include` block (before line 12's first `#define`), add:

```c
#define RECEIVER_REF_SLOTS CHIAKI_VIDEO_RECEIVER_REF_SLOTS
```

- [ ] **Step 6: Update `add_ref_frame()` in `videoreceiver.c`**

Current (lines 22-38):
```c
static void add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	if(video_receiver->reference_frames[0] != -1)
	{
	memmove(&video_receiver->reference_frames[1], &video_receiver->reference_frames[0], sizeof(int32_t) * 15);
	video_receiver->reference_frames[0] = frame;
	return;
	}
	for(int i=15; i>=0; i--)
	{
	if(video_receiver->reference_frames[i] == -1)
	{
		video_receiver->reference_frames[i] = frame;
		return;
	}
	}
}
```

Replace with:
```c
static void add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	if(video_receiver->reference_frames[0] != -1)
	{
	memmove(&video_receiver->reference_frames[1], &video_receiver->reference_frames[0],
		sizeof(int32_t) * (RECEIVER_REF_SLOTS - 1));
	video_receiver->reference_frames[0] = frame;
	return;
	}
	for(int i = RECEIVER_REF_SLOTS - 1; i >= 0; i--)
	{
	if(video_receiver->reference_frames[i] == -1)
	{
		video_receiver->reference_frames[i] = frame;
		return;
	}
	}
}
```

- [ ] **Step 7: Update `have_ref_frame()` in `videoreceiver.c`**

Current (lines 40-44):
```c
static bool have_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	for(int i=0; i<16; i++)
	if(video_receiver->reference_frames[i] == frame)
		return true;
	return false;
}
```

Replace with:
```c
static bool have_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	for(int i = 0; i < RECEIVER_REF_SLOTS; i++)
	if(video_receiver->reference_frames[i] == frame)
		return true;
	return false;
}
```

- [ ] **Step 8: Update the alternate-reference scan upper bound in `flush_frame`**

Current line 467:
```c
			for(unsigned i=slice.reference_frame+1; i<16; i++)
```

Replace with:
```c
			for(unsigned i = slice.reference_frame + 1; i < RECEIVER_REF_SLOTS; i++)
```

- [ ] **Step 9: Verify `chiaki_video_receiver_init` uses `sizeof()` not a hardcoded 16**

```bash
grep -n "reference_frames\|memset.*reference" lib/src/videoreceiver.c | head -20
```

If `chiaki_video_receiver_init` uses `memset(video_receiver->reference_frames, -1, sizeof(video_receiver->reference_frames))`, no change is needed — `sizeof` auto-adjusts. If you see `* 16` or `* 64`, update to `sizeof(video_receiver->reference_frames)`.

- [ ] **Step 10: Format and build**

```bash
./tools/build.sh format
./tools/build.sh --env testing
```

Expected: clean build, `VitakiForkv0.1.785.vpk` produced, no new compiler warnings. If warnings appear about array size or memmove length, fix them before continuing.

- [ ] **Step 11: Run test suite**

```bash
./tools/build.sh test
```

Expected: all existing tests pass. (The ref-frame functions are `static` and not directly unit-tested; correctness is verified via logs in Step 12.)

- [ ] **Step 12: Commit**

```bash
git add lib/include/chiaki/videoreceiver.h lib/src/videoreceiver.c tools/build.sh vita/include/version.h
git commit -m "$(cat <<'EOF'
fix(video): align receiver ref-frame window to 8 slots matching Vita HW decoder DPB

The receiver tracked 16 reference-frame slots while the Vita HW H.264
decoder (sceAvcdec) retains only 8 frames in its DPB (REF_FRAMES=8).
A P-frame referencing a slot 9-16 would pass the receiver's have_ref_frame
check but arrive at the decoder after it had evicted that frame — producing
silent macroblocking with no error signal from either side.

Shrinking the receiver window to 8 makes have_ref_frame return false for
those frames, activating the existing alternate-reference rewrite and
cascade-skip/IDR recovery path rather than silently decoding garbage.

Adds CHIAKI_VIDEO_RECEIVER_REF_SLOTS=8 constant to keep the HW/lib
relationship documented in one place.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 13: Push and open PR**

```bash
git push -u origin fix/receiver-ref-slots-8
gh pr create \
  --title "fix(video): align receiver ref-frame window to 8 slots matching Vita HW decoder DPB" \
  --body "$(cat <<'EOF'
## Summary
- Reduces `reference_frames[16]` → `reference_frames[8]` in `ChiakiVideoReceiver`
- Updates `add_ref_frame`, `have_ref_frame`, and the alternate-ref scan to use `RECEIVER_REF_SLOTS = 8`
- Adds `CHIAKI_VIDEO_RECEIVER_REF_SLOTS` to `videoreceiver.h` with a comment linking it to `vita/include/video.h:REF_FRAMES`

## Root cause fixed
The receiver's 16-slot store approved P-frames referencing frames 9-16 ago. The Vita HW decoder's DPB only holds 8 frames, so those decodes produced silent macroblocking. See `docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md` §4 Cause #1.

## Test plan
- [x] `./tools/build.sh test` — all tests pass
- [x] `./tools/build.sh --env testing` — clean build
- [ ] On-device: stream a motion-heavy game; check that `PIPE/STAGE skips=` count does not increase relative to baseline and that macroblocking duration shortens (or disappears)
- [ ] On-device: check logs for `"Missing reference frame"` entries — these should now appear for old-ref P-frames (previously silent); confirm `cascade_skip` fires and IDR recovery completes

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 14: On-device validation before merging**

Deploy to Vita:
```bash
./tools/build.sh --env testing
./tools/build.sh deploy <vita_ip>
```

Test procedure:
1. Start a remote play session (PS5, 540p@30fps default config).
2. Find a game with obvious motion (fast camera pan, racing game, anything with sudden movement).
3. Induce the bug on the old build first (baseline): observe that motion triggers macroblocking lasting >1 second.
4. Flash the new build. Repeat the motion test.

Pass criteria:
- `grep "Missing reference frame" <log>` — entries now appear when motion starts (previously silent). This confirms the fix is active.
- `grep "PIPE/STAGE" <log>` — `skips=` count is ≤ baseline (not blowing up). If skips spike, the PS5 is sending references beyond 8 frames ago under normal conditions — file a bug and revert.
- Visual: macroblocking either gone or shorter (cascade fires and IDR returns an I-slice within ~300-500ms after motion start).

If on-device passes, merge. Then cut Task 2 branch from the new main.

---

## Task 2: Request IDR on first unrecoverable missing reference (not third)

> **Prerequisite:** Task 1 merged to main and on-device validated. Cut this branch from main after merge.

**Files:**
- Modify: `lib/src/videoreceiver.c:481-491` (the `!recovered` block in `flush_frame`)

**Interfaces:**
- `video_receiver_maybe_request_idr(ChiakiVideoReceiver *, uint64_t now_ms, const char *reason)` — already defined at videoreceiver.c:120. The 100ms cooldown (`IDR_REQUEST_COOLDOWN_MS=100`) and 1000ms timeout (`IDR_REQUEST_TIMEOUT_MS=1000`) are unchanged; they prevent flooding automatically.
- No new functions or types.

- [ ] **Step 1: Create feature branch from current main**

```bash
git checkout main && git pull
git checkout -b fix/idr-on-first-missing-ref
```

- [ ] **Step 2: Read the current `!recovered` block**

Open `lib/src/videoreceiver.c` and locate the `!recovered` block (around line 481 after Task 1 — line numbers may shift slightly). It currently looks like:

```c
			if(!recovered)
			{
				succ = false;
				video_receiver->frames_lost = saturating_add_u32(video_receiver->frames_lost, 1U);
				chiaki_stream_connection_report_missing_ref(&video_receiver->session->stream_connection);
				video_receiver->consecutive_missing_ref++;
				CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d (cascade=%u)",
					(int)ref_frame_index, (int)video_receiver->frame_index_cur,
					video_receiver->consecutive_missing_ref);
				// Keep missing-ref handling passive to avoid aggressive restart/keyframe churn.
			}
```

- [ ] **Step 3: Add IDR request on first missing ref**

Replace the `!recovered` block with:

```c
			if(!recovered)
			{
				succ = false;
				video_receiver->frames_lost = saturating_add_u32(video_receiver->frames_lost, 1U);
				chiaki_stream_connection_report_missing_ref(&video_receiver->session->stream_connection);
				video_receiver->consecutive_missing_ref++;
				CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d (cascade=%u)",
					(int)ref_frame_index, (int)video_receiver->frame_index_cur,
					video_receiver->consecutive_missing_ref);
				// Request IDR immediately; cooldown (IDR_REQUEST_COOLDOWN_MS=100ms) prevents
				// flooding. Cascade skip fires at consecutive_missing_ref >= CASCADE_SKIP_THRESHOLD
				// as a backstop if the PS5 is slow to respond.
				uint64_t idr_now_ms = chiaki_time_now_monotonic_ms();
				video_receiver_maybe_request_idr(video_receiver, idr_now_ms, "missing_ref");
			}
```

- [ ] **Step 4: Format and build**

```bash
./tools/build.sh format
./tools/build.sh --env testing
```

Expected: clean build, version bumped to next iteration, no new warnings.

- [ ] **Step 5: Run test suite**

```bash
./tools/build.sh test
```

Expected: all existing tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/src/videoreceiver.c tools/build.sh vita/include/version.h
git commit -m "$(cat <<'EOF'
fix(video): request IDR on first unrecoverable missing ref, not third

Previously the missing-ref path was passive: it only counted consecutive
failures and relied on CASCADE_SKIP_THRESHOLD=3 to eventually request a
keyframe. With the existing 100ms IDR cooldown that means at least 300ms
of macroblocking before recovery begins — and up to 1s if an IDR request
is already in flight.

Requesting IDR immediately on the first unrecoverable missing reference
front-loads recovery by 2 frames (~66ms at 30fps). The cooldown ensures
at most 10 IDR requests/second; the cascade skip at depth 3 still fires
as a backstop.

Follows Task 1 (receiver ref-slot alignment, PR #186) which makes
missing-ref detection correct in the first place.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Push and open PR**

```bash
git push -u origin fix/idr-on-first-missing-ref
gh pr create \
  --title "fix(video): request IDR on first unrecoverable missing ref, not third" \
  --body "$(cat <<'EOF'
## Summary
Adds a `video_receiver_maybe_request_idr(..., "missing_ref")` call inside the `!recovered` block of `flush_frame`. The existing 100ms cooldown prevents flooding. The cascade skip at depth 3 still fires as a backstop.

## Root cause fixed
The recovery path waited for 3 consecutive unrecoverable missing refs before requesting an IDR. Each ref failure is ~33ms at 30fps; waiting for 3 means ~100ms of certain macroblocking before the PS5 is even asked for a keyframe, plus PS5 response latency. See `docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md` §4 Cause #2.

## Dependency
Requires Task 1 (PR #186 — receiver ref-slot alignment) to be merged first. Without Task 1, the missing-ref path fires less often (old refs 9-16 slots ago silently pass), so this change has reduced impact.

## Test plan
- [x] `./tools/build.sh test` — all tests pass
- [x] `./tools/build.sh --env testing` — clean build
- [ ] On-device: stream a motion-heavy game. Check `grep "Requesting IDR (missing_ref)" <log>` — entries should appear within 1 frame of motion macroblocking onset (vs. 3 frames previously).
- [ ] On-device: confirm `grep "PIPE/STAGE" <log>` `skips=` count does not increase vs. Task 1 baseline. Increased skip count would indicate the PS5 is not responding to IDR requests quickly enough — acceptable; does not regress stability.
- [ ] Visual: macroblocking recovery faster than Task 1 baseline.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 8: On-device validation before merging**

Deploy:
```bash
./tools/build.sh --env testing
./tools/build.sh deploy <vita_ip>
```

Test procedure (motion-heavy game as in Task 1):
1. Capture log from a session with deliberate motion that previously caused macroblocking.
2. `grep "Requesting IDR" <log>` — look for `missing_ref` entries appearing close to motion onset, not just `cascade_skip`.
3. Count frames between first `"Missing reference frame"` log and first `"Requesting IDR"` for that session. Should be 1 (immediate) vs. 3 (previous).
4. Visual: shorter delay to clean frames after motion start.

**Watch for regression:** if `Requesting IDR (missing_ref)` fires more than ~5 times per second during otherwise-stable streaming (no motion), the PS5 is sending references beyond 8 frames under normal conditions. In that case, limit the IDR request to `consecutive_missing_ref == 1` only (i.e., only on the SECOND occurrence, giving one miss as grace). Add that guard:
```c
if(video_receiver->consecutive_missing_ref == 1)
{
    uint64_t idr_now_ms = chiaki_time_now_monotonic_ms();
    video_receiver_maybe_request_idr(video_receiver, idr_now_ms, "missing_ref");
}
```

Merge once stable.

---

## Post-merge checklist

- [ ] Update `DONE.md` with both PRs and on-device validation results.
- [ ] Update `TODO.md` to close the loss-mitigation task and note remaining work (FEC parity data collection — Cause #3 in the investigation doc).
- [ ] File a follow-up GH issue for Cause #3 (no client-side loss/bitrate adaptation) and Cause #4 (HW decoder silent corrupt output), linking to `docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md`.
