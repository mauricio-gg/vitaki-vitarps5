# Handoff prompt — GH #251 (copy everything below into a fresh context)

---

I need you to research and plan a fix for a PS Vita Remote Play video bug. Repo: `/Users/mauriciogaldos/Developer/AndeanBear/vitarps5` (a Chiaki fork). Read `CLAUDE.md` first — the orchestrator/subagent workflow is mandatory. Branch `diag/idr-roundtrip-251` is checked out.

**Do not re-derive the findings below.** They come from three hardware capture sessions and are supported by measurement, not inference. Take them as given and spend your effort on the open question.

## The symptom

During play, the picture freezes for roughly a second at a time. On a bad stretch this happens about every 13 seconds. The user experiences it as input lag. The PS5's own monitor shows the game responding instantly throughout, so the console is receiving input and rendering fine — the delay is entirely in video delivery to the Vita.

## The mechanism (established)

1. Wi-Fi drops a burst of packets; **one video frame fails FEC** and is lost.
2. Every subsequent P-frame references it. With the reference gone, the client sets `succ = false` and **discards each one** — 26 consecutive frames in the worst measured case.
3. The client requests an IDR (keyframe).
4. **The PS5 takes 818–1499 ms to deliver it.** Measured, all 9 events in one session: `942, 1032 (timed out, none arrived), 1183, 914, 973, 1240, 818, 1499` ms.
5. The screen holds the last good frame for that entire window.

Nine such events in a two-minute session ≈ 9 seconds of frozen video.

## What is already ruled out — do not re-investigate

| Suspect | Evidence |
|---|---|
| Vita decode/display path | `decode_avg_ms` 1.7 at window 8 and 1.7 at window 112; `display_fps` tracks `incoming`; 4 frame overwrites in 2 min; 0 decode-queue drops; `stuck_streak`/`cascade_streak` 0 |
| Client socket backlog / recv-thread stall | Audio rides the **same socket and same recv thread**; audio queue held flat at 1.50 with zero catchup events straight through the 757 ms video gap. Also `drain_max=1` |
| Console send-pacing (VBR through a byte-rate pacer) | Gap after a large frame vs a small frame: **−0.09 ms** across 3,376 attributed gaps |
| Client Wi-Fi power-save batching | Burst-release signature (gaps < 10 ms) = 5 of 3,543 gaps (0.14%) |
| PS5 bitrate ratchet (#237) | Frame size 2.59 Mbps before the episodes → 2.78 Mbps after. Did not occur |
| Loss over-reporting (#238) | 0 of 119 windows had `reported_precap > raw_lost`; session loss 0.17% |
| PS5 ignoring our IDR request / fixed GOP schedule | Keyframes appear 1–2 s after every request, and there are 16–25 second stretches with **zero** keyframes when we don't ask |
| Our IDR request being lost | Sent via `chiaki_takion_send_buffer_push`; `TAKION_DATA_RESEND_TIMEOUT_MS` = 200 ms × 10 tries (`lib/src/takionsendbuffer.c:13`) |

Steady-state delivery is ordinary link smear of about ±10 ms and is **not** the problem.

## The client-side bug found so far

`lib/src/bitstream.c` — `chiaki_bitstream_slice_set_reference_frame()`:

```c
if(bitstream->codec == CHIAKI_CODEC_H264)
    return false;          // rewriting is implemented for H.265 only
```

The Vita is H.264-only (`lib/src/session.c:111` pins the codec; the decoder is `SCE_VIDEODEC_TYPE_HW_AVCDEC`, `vita/src/video.c`). So the alternate-reference recovery block in `lib/src/videoreceiver.c` — the thing meant to re-point a frame at an older reference so the picture keeps moving — **has never executed. 0 successes in 112 attempts.** The comment above it promises "brief visual artifacts, not a blackout"; you get the blackout.

Naively implementing it is not viable: all 112 observed slices carry `slice.reference_frame = 0`, i.e. the PS5 emits no `ref_pic_list_modification`. The H.265 path works by flipping one pre-existing bit; H.264 would require *inserting* syntax elements and bit-shifting ~12 KB of slice payload per frame on the latency-critical Takion recv thread. Verify this yourself before accepting it.

## Your task

**Work out the right fix for the underlying problem, and produce a plan. Do not write production code yet.**

Research both the codebase and the wider world. Specific directions worth pursuing — evaluate them, don't assume any is correct:

1. **Reference-frame invalidation instead of a full IDR.** This is the strongest lead. The codebase already calls `chiaki_stream_connection_report_corrupt_frame()`, `report_fec_fail()`, and `report_missing_ref()`. Enumerate every `tkproto_TakionMessage_PayloadType_*` in the protobuf definitions and find out what the PS5 actually does with each. If the protocol supports telling the console "frame N is bad, encode the next frame against an older good one", that is dramatically cheaper than a keyframe and would collapse the ~1 s window. Compare with how NVIDIA GameStream/Moonlight handles this (they call it RFI — reference frame invalidation) and with Sunshine's implementation. Check whether upstream Chiaki, chiaki-ng, or any fork has explored this.

2. **Why the console takes ~1 s.** Nothing in our data explains it — the largest frame all session is 30,792 bytes ≈ 91 ms on the wire. Research whether Sony's encoder has a known keyframe latency, whether it depends on the negotiated profile, and whether other Remote Play clients see the same. If it is irreducible, the fix must be to stop freezing during the wait.

3. **Not freezing during the wait.** Currently a P-frame with a missing reference is dropped outright. Submitting it anyway would give macroblock artifacts but preserve motion — probably better for a responsiveness complaint. Research what `sceAvcdecDecode` does with a broken reference chain, whether the Vita's hardware decoder does error concealment, and the risk to its DPB state. This project already knows the DPB is fragile (dropping compressed P-frames pre-decode breaks it), so treat this as risky and design an A/B behind a config toggle.

4. **Whether periodic intra-refresh is negotiable.** If the stream can be asked for periodic intra refresh or long-term reference frames, loss stops being catastrophic in the first place. Look at `lib/src/launchspec.c` and the negotiated video profile for anything exposed.

Use web research for the protocol and encoder questions — the answers are unlikely to be inferable from this repo alone.

## Also worth knowing

- Six unreachable code paths are annotated on this branch — `grep -rn DEAD-CODE lib vita`. Two candidates were checked and deliberately **not** marked (the takion overflow handler and the loss-gate counter are reachable, just a no-op and a self-cap). Trust these annotations; they were verified against hardware logs.
- `docs/ai/wip/251-idr-probe-incomplete.patch` holds an unfinished diagnostic probe (per-window I-slice counts, IDR request counts, FEC-failure unit sizes). It does **not** compile — a constants block was deleted while three references to it remain. Reuse the ideas if useful; do not apply it blind. Its purpose was to distinguish "the console answers but the keyframe reply dies in the same loss burst" from "the console is genuinely slow". That question is still open and may be worth settling before or alongside any fix.
- **`./tools/build.sh test` only cross-compiles and never executes** (GH #222). A green result proves nothing about assertions. Re-run pure-function tests natively with `cc` for real coverage.
- Hardware logs live in the repo root as `*_vitarps5-testing.log`. The richest is `92941169310_vitarps5-testing.log`. `PIPE/DELIVERY` lines come from PR #256 (open, branch `diag/frame-delivery-pattern-251`, which this branch is based on).
- Relevant open issues: #251 (this), #237, #238, #239, #241, #253, #254, #255, #222.
- Build with `./tools/build.sh --env testing` for anything going to hardware. Run `./tools/build.sh format` before committing.

Start by reading `CLAUDE.md`, then `lib/src/videoreceiver.c` and `lib/src/bitstream.c`. Ask me questions before settling on an approach.
