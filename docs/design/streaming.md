# Streaming pipeline and recovery

What the code on `main` does today between "a UDP packet arrives from the console" and "a picture is on the screen", and what happens when the stream dies. For the list of threads and who owns them, see [architecture.md](architecture.md). Citations are `path:line` against the tree at the time of writing; line numbers drift, function names are the stable handle.

## 1. Pipeline at a glance

```
 PS5 --UDP--> [Takion recv thread, USER_0 then USER_2 after first audio frame, prio 64]
                 | control packets -> reorder queue (256) -> stream connection
                 | video packets -> chiaki_video_receiver_av_packet()
                 |       frame assembly + FEC + reference checks
                 |       host_video_cb() -> vita_h264_decode_frame()  (producer)
                 v
        decode_queue[4]  (SPSC ring, decode_q_mtx / decode_q_cond)
                 |
                 v
        [VitaDecode thread, USER_1, prio 64]  sceAvcdecDecode -> frame_texture
                 |  frame_ready_for_display = true
                 v
        [UI thread]  vita_video_render_latest_frame() -> vita2d draw + swap

 audio packets -> decoded inline on the recv thread (vita_audio_cb); no separate audio thread
 loss stats    -> [congestion control thread, every 200 ms] -> PS5
 input         -> [feedback sender thread, prio 65] -> PS5
```

The recv thread never waits on the decoder for a whole frame: it copies the compressed frame into a queue slot and returns (section 5).

## 2. Receive (Takion)

- **Thread.** `takion_thread_func` is created at `lib/src/takion.c:581` (`chiaki_thread_create`). On Vita it sets itself to priority `TAKION_RECV_THREAD_PRIORITY` = 64 and pins to CPU `USER_0` (`takion.c:59-65` define, `takion.c:1277-1278` apply). Decode runs on `USER_1`. The audio callback also runs on this thread and pins it to `USER_2` on the first audio frame (section 6).
- **Socket buffer.** On Vita the advertised window `TAKION_A_RWND` is `0x80000` (512 KB, `takion.c:51`), copied into `takion->a_rwnd` (`takion.c:330`) and applied with `setsockopt(SO_RCVBUF)` in both connect paths (`takion.c:370`, `takion.c:491`). The kernel's actual value is read back and logged as `Takion SO_RCVBUF requested=... actual=...` (`takion.c:380-381`, `501-502`). The same value is used for `SO_SNDBUF` (`takion.c:386`, `507`).
- **Drain loop.** After the blocking `takion_recv` wakes the thread, it pulls up to `TAKION_RECV_DRAIN_MAX` = 256 more packets with zero-timeout reads before blocking again (`takion.c:80`, loop at `takion.c:1432-1441`).
- **Packet dispatch.** `takion_handle_packet` (`takion.c:1810`) sends control packets to the message path and video/audio packets straight to `takion_handle_packet_av` (`takion.c:1820-1828`). If encryption is on and the remote key is not ready yet, AV packets are parked in a postpone list instead (`takion.c:1825-1826`).
- **The reorder queue is control-channel only.** `takion->data_queue` (256 entries, `TAKION_REORDER_QUEUE_SIZE_EXP` 8, `takion.c:69`, init at `takion.c:1285`) is fed from the message path. The comment at `takion.c:1071` states video and audio never enter it, so its overflow/drop log lines say nothing about video loss.
- **Transient errors (ENOBUFS) are not fatal.** In `takion_recv` (`takion.c:1616` onward) a transient error from `select()` or `recv()` is counted, logged at most once a second (`TAKION_RECV_TRANSIENT_LOG_INTERVAL_MS` = 1000), and retried after a backoff of `TAKION_RECV_TRANSIENT_RETRY_DELAY_MS` = 5 ms (`takion.c:140-143`). The non-blocking drain call (timeout 0) only counts and returns, it never sleeps (`takion.c:1641`, `1688`). The call gives up with `CHIAKI_ERR_NETWORK` only after more than `TAKION_RECV_TRANSIENT_MAX_CONSECUTIVE` = 400 consecutive hits AND at least `TAKION_RECV_TRANSIENT_MIN_PERSIST_MS` = 1500 ms since the first (`takion.c:1644-1646`, `1691-1693`). Any successful receive resets the count.
- **Why the backoff has a fallback.** `takion_recv_transient_backoff` (`takion.c:1606`) first waits on the stop pipe. On Vita the stop pipe is itself a socket, so during an ENOBUFS storm that wait fails instantly; when it does not report a timeout or a cancel, the function falls back to `chiaki_sleep_ms`. Without that, the retry budget was burned in a tight spin (explained in the comment at `takion.c:113-139`). Giving up is what ends the session as a transport failure (section 8).
- **The "jitter" number is not network jitter.** `takion->jitter_stats.jitter_us` is an EWMA (weight 1/8) of how much the gap between consecutive control-channel packets changes (`takion.c:2171-2176`). Video and audio never touch it. Video has its own, separate figure (`video_jitter_stats`, `takion.c:2454`). The log line starting `Latency metrics` prints `senkusha_rtt` (a single round trip measured during the handshake, never updated afterwards, `host_metrics.c:712-717`) and `ctrl_cadence_jitter` (that control-channel EWMA); the line itself says "NOT real network jitter". Neither is a live round trip. The console's own round-trip estimate, which it sends in its connection-quality messages, is shown by `PIPE/CONSOLE` (section 9.4).

## 3. Video assembly and FEC

`chiaki_video_receiver_av_packet` (`lib/src/videoreceiver.c:416`) runs on the recv thread for every video packet.

- **Frame boundaries.** A packet with a lower frame index than the current one is dropped as an "old frame packet" (`videoreceiver.c:425-430`). A higher index flushes the previous frame first, then starts a new one (`videoreceiver.c:462-470`). A jump of more than one frame index from the last completed frame is a gap (handled right after, `videoreceiver.c:471` onward).
- **Gap reports to the console.** A gap is held `VIDEO_GAP_REPORT_HOLD_MS` = 24 ms so late packets can fill it, or reported at once if it spans at least `VIDEO_GAP_REPORT_FORCE_SPAN` = 12 frames (`videoreceiver.c:15-17`). Re-reports of a growing range during one stall are rate-limited by `VIDEO_CORRUPT_REPORT_COOLDOWN_MS` = 500 ms, unless the range grew by `VIDEO_CORRUPT_REPORT_GROWTH_BYPASS_SPAN` = 32 frames (`videoreceiver.c:37`, `48`). The pure gap bookkeeping lives in `lib/src/videoreceiver_gap.c` (`chiaki_video_gap_report_update`). `VIDEO_SPAN_SANITY_MAX` = 4096 guards against corrupt sequence state (`videoreceiver.c:19`).
- **Units and FEC.** `lib/src/frameprocessor.c` collects the units of one frame (`chiaki_frame_processor_alloc_frame`, `put_unit`). A flush is allowed once source units plus parity units received reach the number of source units expected (`videoreceiver.c:638`). If all source units are present, the frame is simply concatenated. If some are missing, `chiaki_frame_processor_flush` (`frameprocessor.c:382`) runs FEC (`chiaki_frame_processor_fec`, `frameprocessor.c:315`), which calls `chiaki_fec_decode` (`frameprocessor.c:346`), a Reed-Solomon decode built on Jerasure with a Cauchy matrix (`lib/src/fec.c:5-6`, `14`). Possible results: delivered, FEC-recovered, FEC-failed, failed.
- **Missing references.** After a successful flush, `chiaki_video_receiver_flush_frame` (`videoreceiver.c:643`) parses the H.264 slice. For a P-slice it checks that the frame it points to is still in the receiver's reference table (`have_ref_frame`, `videoreceiver.c:803`). If not, it tries to rewrite the slice to point at an older frame we do have (`chiaki_bitstream_slice_set_reference_frame`, `videoreceiver.c:808-820`). On H.264 that function always returns `false` (`lib/src/bitstream.c:457-458`, the Vita is H.264-only), so this rewrite never succeeds on this build and the code continues to the missing-reference handling in section 4.
- **IDR constants (where they actually live).** `IDR_REQUEST_COOLDOWN_MS` = 100, `IDR_REQUEST_TIMEOUT_MS` = 1000 and `CASCADE_SKIP_THRESHOLD` = 3 are defined at `videoreceiver.c:49-51` (not lines 18-19; those now hold the gap-report constants). Their use is in `video_receiver_maybe_request_idr` (`videoreceiver.c:239`): if a request is pending for more than the timeout, the pending flag is cleared; a new request is sent only if none is pending and at least the cooldown has passed since the last one. A pending request is cleared when an I-slice is received (`videoreceiver.c:782-790`).

## 4. Loss handling

When a frame cannot be used as-is, what happens depends on why.

- **FEC failed.** The frame is discarded, an IDR is requested (reason `"fec_failed"`, `videoreceiver.c:715`), the console is told which frames are corrupt (`report_corrupt_frame_range`, `videoreceiver.c:738`), and `frames_lost` is increased by the span (`videoreceiver.c:739-745`).
- **Missing reference, default behaviour.** `succ` becomes false, so the frame is not given to the decoder; `frames_lost` and `consecutive_missing_ref` go up and an IDR is requested (`videoreceiver.c:821-860`). After `CASCADE_SKIP_THRESHOLD` (3) such frames in a row, `chiaki_video_receiver_flush_frame` skips flush and decode entirely, resets the reference table (`video_receiver_apply_cascade_reset`, `videoreceiver.c:276`) and keeps asking for an IDR (`videoreceiver.c:658-679`).
- **Submit-through-loss (PR #257), config-gated and OFF by default.** If the config key `submit_on_missing_ref` is true and the stream is H.264 with `gaps_in_frame_num_value_allowed_flag == 0` in its SPS (`chiaki_bitstream_h264_drift_safe`, `bitstream.c:463-469`), a missing-reference P-frame is still passed to the decoder (`videoreceiver.c:821-848`). The lost frame was never submitted, so the decoder's newest reference is the last good frame, and the P-frame is decoded against that. The picture may drift until the next I-slice, which is requested at once. The frame is not counted as lost and does not feed the cascade counter. The key defaults to `false` (`vita/src/config.c:57`, parsed at `config.c:401`) and is passed to the library at `vita/src/host.c:387` (`session.c:267`). So on a default install the behaviour in the previous bullet is what runs.
- **What the console is told.** `lib/src/congestioncontrol.c` runs a thread that wakes every `CONGESTION_CONTROL_INTERVAL_MS` = 200 ms, reads and resets the packet statistics, and sends a congestion packet with `received` and `lost` (`congestioncontrol.c:5`). The statistics are filled per frame by `chiaki_frame_processor_report_frame_stats` (`frameprocessor.c:235`) using source units only, never parity (explanation at `frameprocessor.c:249-262`, from PR #213): a frame with all source units counts as lossless; an FEC-recovered frame reports its missing source units as "recovered", not lost; an FEC-failed or skipped frame reports them as lost. In the congestion thread the reported loss is `lost + recovered / 4` (`CONGESTION_FEC_RECOVERED_LOSS_DIVISOR`, so light recoverable loss rounds to zero) and is capped at 10 % of the total (`CONGESTION_MAX_REPORTED_LOSS`, `congestioncontrol.c:11-16`, `44-57`). It matters because the PS5 lowers its bitrate whenever it hears about loss, so inflating the number (for example by counting FEC parity as missing) walks the bitrate down for no reason. A compile-time switch `VITARPS5_CONGESTION_PARITY_INCLUSIVE_RECEIVED` exists for an A/B experiment and is off in the normal build (`congestioncontrol.c:112-116`).
- **Feedback sender.** `lib/src/feedbacksender.c` sends controller state to the console. On Vita it runs at priority 65 (`FEEDBACK_SENDER_THREAD_PRIORITY`, `feedbacksender.c:31`, applied at `413`): below the three 64 threads on purpose, so it never ties with the recv, decode or audio threads.
- **Freeze-then-jump (PR #263): gone.** The presentation hold driven by arrival cadence (the "#262 stale-hold gate") was removed in PR #280 (commit `0e9dd13b`); `vita/src/video.c` no longer contains it. What remains on the display side is the simpler freeze-on-corrupt in section 6.

## 5. Decode (producer / consumer)

All in `vita/src/video.c`.

- **Producer.** The library's video callback `host_video_cb` (`vita/src/host_callbacks.c`) marks the frame corrupt if `frames_lost > 0` or it was a recovered frame (`host_callbacks.c:129`), then calls `vita_h264_decode_frame` (`host_callbacks.c:146`, `video.c:985`) on the recv thread. That function validates the size (rejects under 5 bytes or over slot capacity minus pad), copies the bitstream into the next slot of `decode_queue` plus a 64-byte zero pad, and signals `decode_q_cond`.
- **Queue.** `DECODE_QUEUE_DEPTH` = 4 slots (`video.c:389`), each `DECODE_SLOT_CAPACITY` = 256 KB (`video.c:392`), allocated once in `vita_h264_start` (`video.c:1386` onward). One slot is always held by the decoder, so at most 3 frames wait.
- **When full.** The producer first waits up to 10 ms for the consumer to free a slot (`chiaki_cond_timedwait`, `video.c:1015-1017`). If still full, it drops the oldest queued slot by advancing the head and counts it in `decode_queue_drops` (`video.c:1020-1025`). The comment at `video.c:1007-1013` records the reason for waiting first: dropping a compressed P-frame before decode breaks the decoder's reference chain, which is worse than a short stall.
- **Consumer.** `decode_thread_func` (`video.c:1326`) is created in `vita_h264_start` (`video.c:1447`, named "VitaDecode"). It sets priority `VITA_DECODE_THREAD_PRIORITY` = 64 (`vita/include/video.h:39`) and CPU `USER_1` (`video.c:1330-1331`), waits on `decode_q_cond`, and takes the head slot's data without advancing the head (`video.c:1350`). It runs `decode_frame_now` (`video.c:900`, calls `sceAvcdecDecode` at `video.c:925`), and only afterwards advances `decode_q_head` and signals (`video.c:1370-1380`). Keeping the head put during decode means the producer's "full" test cannot reuse the slot being decoded. Decode always runs, even for frames flagged corrupt, to keep the hardware decoder's reference state in step (`host_callbacks.c:123-127`).
- **Timestamp.** The access unit's presentation timestamp is the frame's running number (upper half 0, lower half the number); before #413 it was `0xFFFFFFFF` in both halves. The decode timestamp (`dts`) is unchanged. The number is read back from the output picture to see which frame came out (section 9.2).
- **Handoff to the UI.** `decode_frame_now` stores the corrupt flag with the decoded pixels under `mtx`, then sets `frame_ready_for_display` (`video.c:967-976`).

## 6. Display and audio

- **UI thread.** `vita_video_render_latest_frame` (`video.c:1105`) returns at once if streaming is not active or `frame_ready_for_display` is false (`video.c:1120-1127`), otherwise clears the flag and picks a texture:
  - clean frame: `promote_decoded_frame_to_last_good` swaps the two fixed textures `frame_texture` and `last_good_texture` (a pointer swap under `mtx`, no copy, `video.c:293-307`), and the new picture is shown;
  - corrupt frame and a last-good texture exists and fewer than `FREEZE_MAX_STREAK` = 8 corrupt frames in a row (`video.c:179`): the last good picture is shown again instead (`video.c:1200-1208`);
  - corrupt for 8 in a row, or no last-good texture: the decoded (possibly broken) frame is shown, so the picture always resumes (`video.c:1219-1224`).
  It then draws (`draw_streaming`), calls `vita2d_wait_rendering_done()` and `vita2d_swap_buffers()` (`video.c:1246-1247`). The optional `force_30fps` pacing can skip frames before this point (`should_drop_frame_for_pacing`, `video.c:311`).
- **Audio.** There is no separate audio thread. Audio packets take the same recv-thread path as video (`takion_handle_packet_av`, `takion.c:1828`, then `streamconnection.c:1280`, `audioreceiver.c:123`, `opusdecoder.c:95`) and `vita_audio_cb` (`vita/src/audio.c:204`) is called inline, with no hand-off and no video queue. On its first call it changes the CALLING thread, i.e. the recv thread, to priority `VITA_AUDIO_THREAD_PRIORITY` = 64 (`vita/include/audio.h:5`) and CPU `USER_2` (`audio.c:207-208`). So the recv thread starts on `USER_0` (`takion.c:1277-1278`) and, from what the code shows, is re-pinned to `USER_2` once the first audio frame arrives. This is unverified on hardware; the comments at `takion.c:1273-1276` and `video.c:1329` assume two threads. See [architecture.md](architecture.md).
- **Thread priorities in one place.** `host.c` logs `PIPE/PRIORITY decode=64 audio=64 recv=64 feedback=65 input=96` (`host.c:514`). `audio=64` is the priority `vita_audio_cb` applies to the calling (recv) thread, not a separate thread. Lower number means higher priority on Vita.

## 7. Teardown order

`host_shutdown_media_pipeline` (`vita/src/host_lifecycle.c:10`) must run in this order:

1. `context.stream.is_streaming = false`, then `vita_h264_stop()`.
2. `sceKernelDelayThread(2000)`.
3. `chiaki_opus_decoder_fini`, then `vita_h264_cleanup()` (frees the decoder and textures, `video.c:827`), then `vita_audio_cleanup()`.

Inside `vita_h264_stop` (`video.c:1476` onward):

- It clears `active_video_thread` first. This is what makes `vita_video_render_latest_frame` bail out (`video.c:1120`); `is_streaming = false` alone is not enough because the UI thread only reads it on its next loop.
- It signals the decode thread to exit and **joins** it (`video.c:1486-1498`), so no `sceAvcdecDecode` is running on a decoder that is about to be freed.
- It frees the queue slots, then polls `ui_render_in_progress` for up to `UI_RENDER_QUIESCENCE_TIMEOUT_US` = 50 ms, logging an error if the UI thread is still inside a render call (`video.c:1471`, `1575-1588`), and only then calls `chiaki_mutex_fini(&mtx)` (`video.c:1590`).

Why the order matters: the decode thread writes into `frame_texture`, the UI thread draws from it and both take `mtx`. Freeing the texture or destroying the mutex while either thread is inside it is a use-after-free (destroying a mutex a thread holds is undefined on the Vita kernel). The comments at `video.c:1528-1548` call the order "load-bearing".

## 8. Recovery after the stream dies

### 8.1 What the library does and does not do

When the transport gives up (for example the ENOBUFS budget in section 2 is exhausted) and the console did not disconnect us, the session ends with `CHIAKI_QUIT_REASON_STREAM_CONNECTION_TRANSPORT_FAILED` (`lib/src/session.c:889-897`). The library no longer tries a soft restart on its own: the old restart ladder and the `CHIAKI_EVENT_STREAM_RESTARTING` event were removed in PR #274 (commit `789415ed`), because a soft restart reuses a control connection the console never releases and every field log showed it timing out (`session.c:793-803`). A restart that someone asks for explicitly through `chiaki_session_request_stream_restart` (`session.c:410`) is still honoured.

On the Vita app, the only code that asks for such a restart is `request_stream_restart` / `request_stream_restart_coordinated` in `vita/src/host_recovery.c:30`, `91`. Their only callers are the stages inside `host_recovery_handle_post_reconnect_degraded_mode` (`host_recovery.c:257`, `285`), and that function returns at its first guard because `post_reconnect_window_until_us` is never set to a non-zero value anywhere (`host_recovery.c:165-180`, writers at `host_callbacks.c:26` and `host_metrics.c:117` both write 0). In practice nothing on `main` requests a soft restart.

### 8.2 The quit handler

`host_handle_quit_event` (`vita/src/host_quit.c:42`) runs on the dying session's own thread. In order:

1. It snapshots everything that decides finalize and recovery before touching the media pipeline, because the pipeline shutdown clears some of it (`host_quit.c:53-60`, `83-90`): `had_streamed`, `fast_restart_active`, `recovery_active`, the attempt counter and the release-poll counter.
2. It decides whether to recover: `recovery_trigger` is true for a transport death (`host_quit.c:102-109`), for a user resync (debug builds), or for a quit that happens while a recovery is already active and the reason is RP_IN_USE or a retryable one. A user stop never triggers it. Recovery is scheduled while `loss_retry_attempts < LOSS_RETRY_MAX_ATTEMPTS` = 2 (`host_quit.c:110`, `vita/include/host_constants.h:4`).
3. It publishes `recovery_active` before the session is marked gone, so `host_in_active_use()` stays true during the hand-off (`host_quit.c:124`, `host.c:109-118`), then calls `host_shutdown_media_pipeline()` (`host_quit.c:125`), then turns the Reconnecting overlay back on immediately so the UI never flashes the main menu (`host_quit.c:128-131`).
4. It cannot join its own thread, so it sets `session_finalize_pending = true` and `session_init = false` and returns (`host_quit.c:139-143`). The UI thread does the join and `chiaki_session_fini` in `host_finalize_deferred_session` (`vita/src/host_lifecycle.c:63`), called from the top of the UI loop (`vita/src/ui.c:520-522`).
5. It stores the schedule: `loss_retry_ready_us`, the attempt count, the cause string, and sets `loss_retry_pending = true` after a memory barrier so the UI thread can never see the pending flag before the finalize flag (`host_quit.c:341-385`, barrier at `384`).

### 8.3 The UI loop starts the reconnect

In `vita/src/ui.c:566-586` the UI thread starts the new connection when all of these hold: `recovery_active`, `loss_retry_pending`, the old session is finalized (`!session_finalize_pending && !session_init`), no connection thread is running, and the clock has passed `loss_retry_ready_us`. If there is no active host or the connection thread cannot start, it calls `host_recovery_abort` (`host_quit.c:418`), which clears all recovery state and shows "Could not reconnect to console". The screen is the Reconnecting screen for the whole episode (`ui.c:665-668`).

The reconnect always uses the normal preset bitrate from `host_default_video_profile` (`host_quit.c:92-95`, `368`; `host.c:193`, `310-318`), never the dying session's bitrate.

### 8.4 State that survives `host_stream()`

`recovery_active`, the attempt count, the release-poll count and the overlay start time are not reset when `host_stream()` starts a new connect (`host.c:278-279` reads them). They are cleared only when the new session reports `CHIAKI_EVENT_CONNECTED` (`host_callbacks.c:59-74`, "Recovery complete"), on abort or on exhaustion (or on a user cancel, which nothing triggers today). The overlay itself is cleared on the first video frame (`host_callbacks.c:114-122`). A later drop after a successful recovery therefore starts a new episode with the full budget of 2.

### 8.5 Timing: the first attempt and the console-release polls (PR #279)

- **First attempt after a transport death.** `ready_us = now + RECOVERY_CONSOLE_RELEASE_DELAY_US`, which is `RETRY_HOLDOFF_RP_IN_USE_MS` = 9000 ms (`host_quit.c:19`, `24`, `341`). The delay is still 9 s here; PR #279 did not change it. (A debug-widget resync skips it: `ready_us = now`, `host_quit.c:342-343`.)
- **If the console still refuses (RP_IN_USE) during recovery.** Instead of waiting out another fixed 9 s, the handler sets `release_poll` (`host_quit.c:114-115`) and schedules the next probe `RECOVERY_RELEASE_POLL_INTERVAL_US` = 1 s later (`host_quit.c:31`, `194-196`, `344-345`). The code comment (`host_quit.c:25-30`) notes a refused probe itself costs about 250 ms. Up to `RECOVERY_RELEASE_POLL_MAX` = 8 polls (`host_quit.c:32`). A poll does not use up the attempt budget and the Reconnecting screen's "Attempt N" does not change (`host_quit.c:364-366`). Each poll logs `Release wait: RP_IN_USE poll n/8 ...` (`host_quit.c:350-355`).
- **After 8 polls.** The next refusal falls back to the old behaviour: it counts as an attempt, logs "polls exhausted ... falling back to 9000 ms holdoff" (`host_quit.c:356-361`), and the holdoff of 9000 ms is armed (`host_quit.c:199-209`). When attempts reach 2, recovery is exhausted: `recovery_exhausted` shows the "Could not reconnect to console" banner and hint and the app lands on the main menu (`host_quit.c:299-306`, `386-395`).
- **Cancel.** The Reconnecting screen has no input and cannot be cancelled (SPEC 3.4); a recovery ends by succeeding, by abort or by exhaustion. There is no user cancel of a recovery (`host_recovery_cancel_by_user` was removed in #382; nothing had called it since the Reconnecting screen was rebuilt in #301). A connect that has not reached `session_init` is still cancelled inside `host_stream()` by the barrier check at `host.c:521-527`.

### 8.6 RP_IN_USE outside a recovery (PR #215 path)

When the console answers a plain user connect with RP_IN_USE, `arm_rp_in_use_auto_retry` (`host_quit.c:176-178`) arms one automatic retry: `rp_in_use_retry_at_us` = now + `RP_IN_USE_AUTO_RETRY_DELAY_US` (6 s, `host_quit.c:36`), or later if a cooldown is still running (`host_quit.c:266-268`). `rp_in_use_retry_used` makes it fire once. The UI loop starts it only when the screen is the main menu and no connection is in flight, and it restores the user's PSN-versus-LAN choice (`ui.c:538-553`). Inside a recovery this path is off (`!restart_context`) because the recovery logic above handles RP_IN_USE.

The post-stop guard applies when the user ends a stream: after a clean disconnect the console acknowledged, a new connect is blocked for `POST_STOP_GUARD_DISCONNECT_ACKED_US` = 2 s; sent but not acknowledged, or never sent after the stream was live, 8 s (`POST_STOP_GUARD_DISCONNECT_UNACKED_US`); a connect cancelled before any stream started has no guard (`host_quit.c:34-35`, `229-258`).

## 9. Delay probes (#413)

Log lines added to find where the delay between a button press and the changed picture lives. They only measure: nothing here changes what is sent or when. All of them print only when `show_latency` is on, once per window next to `PIPE/LATENCY`, `PIPE/INPUT`, `PIPE/DISPLAY` and `PIPE/FPS` (`host_metrics_update_latency`, `vita/src/host_metrics.c`). Each log line has its own subsection below.

### 9.1 `PIPE/PRESS`: from a button press to the changed picture

**What it measures.** For one button press, how long until the console's answer is on the Vita screen, split into four points. The use: sit on a quiet PS5 screen (Settings), press D-pad down about once every two seconds, and the first big video frame after each press is the menu move. Thirty presses should give about thirty samples.

**Clock.** Everything is `sceKernelGetProcessTimeWide()` in microseconds, the same clock as the other `PIPE/` lines. The library's millisecond stamps (`chiaki_time_now_monotonic_ms()`) are the same clock cut to whole milliseconds (`vita/src/video.c`, comment in `vita_video_render_latest_frame`).

**Start point (press sent).** `feedback_sender_thread_func` in `lib/src/feedbacksender.c`: on a wake where the history packet goes out and at least one digital button changed from released to pressed, the time right after the send is stored with a press counter that only goes up. Releases, sticks, touches and the analog L2/R2 values do not count; a press and release gives one event. The receive thread reads it with `chiaki_feedback_sender_read_press_stamp` without any lock (a seqlock, so it never sees a count paired with another press's time).

**The four points**, each in microseconds since the press was sent, for the same video frame (the "jump frame"):

| Field | End point | Hook |
|---|---|---|
| `first_us` | The frame's first packet was read by the receive thread. Only known to 1 ms: the library stamps whole milliseconds, so this reads up to 999 us early. | `cur_frame_first_packet_ms` in `lib/src/videoreceiver.c`, passed in as `frame_first_packet_ms` |
| `assembled_us` | The assembled frame reached the decode hand-off. | `press_meter_observe_frame`, called at the top of `vita_h264_decode_frame` (`vita/src/video.c`); one clock read per frame was added there |
| `decoded_us` | Decoding of the frame that was shown finished. | The decode-done stamp in `decode_frame_now` (`vita/src/video.c`), the same value `PIPE/DISPLAY` uses for its pickup span |
| `shown_us` | The swap that put the picture on screen. | The clock read right after `vita2d_swap_buffers()` in `vita_video_render_latest_frame` (no extra read) |

**Which frame is shown.** The jump frame may never reach the screen: the decode queue can drop it, a newer decoded frame can replace it, `force_30fps` pacing can skip it, or the freeze-on-corrupt path can show the last good picture instead. Frames carry a running number. The number of the picture in `frame_texture` is the one the decoder reports back for it (`PIPE/DECHOLD`, section 9.2) when that read-back is good, and the number that was fed in when it is not; so if the decoder holds frames back, `decoded_us` and `shown_us` still belong to the frame that is really on screen. `shown_us` and `decoded_us` come from the first swap that presents a freshly decoded picture whose number is the jump frame's or newer; in those cases `decoded_us` is the newer frame's decode-done. A pass that only repeats an older picture, or is paced out, does not complete the sample.

**Detection rule** (`vita/src/press_meter.c`, pure logic, tested natively). The detector keeps the sizes of the last `PRESS_METER_QUIET_WINDOW` = 16 quiet frames; the quiet reference is the largest of them. While a press waits, the first frame of at least `PRESS_METER_JUMP_RATIO` = 3 times the reference AND at least `PRESS_METER_JUMP_MIN_EXTRA_BYTES` = 2048 bytes more is the jump. The window must be full before any jump counts. One sample per press: later large frames of the same animation are ignored. A press with no jump within `PRESS_METER_EXPIRY_US` = 1.5 s expires (no sample). A press arriving while another waits is an overlap and is ignored; the oldest keeps waiting. Corrupt frames are ignored entirely (they neither trigger nor enter the quiet window). A large frame whose first packet was read in an earlier millisecond than the press was sent cannot be its answer and is ignored too. The numbers are first guesses, to be tuned from the first hardware log.

**Fields of the line.**

| Field | Meaning |
|---|---|
| `presses` | Presses sent this window. |
| `n` | Completed samples this window. |
| `first_us`, `assembled_us`, `decoded_us`, `shown_us` | The four points above for the newest sample of the window; 0 when `n=0`. |
| `first_max_us`, `shown_max_us` | Window maximum of `first_us` and of `shown_us`; 0 when `n=0`. |
| `frame_bytes`, `quiet_bytes` | Size of the newest sample's jump frame and the quiet reference at that moment, so a human can judge whether it was a real jump. |
| `order_err` | Samples whose points were out of order (they should satisfy 0 <= first <= assembled <= decoded <= shown). Nothing is clamped; the raw values are still reported. |
| `expired` | Presses that saw no jump in time. |
| `overlap` | Presses ignored because another was pending (or because two presses fell between two frames and only the newer was seen). |
| `lost` | Jumps replaced by a newer jump before they were shown (the screen did not swap for over a second). |
| `presses_total`, `samples_total` | Session running totals; the last line of a log answers "thirty presses, how many samples". |

Example: `PIPE/PRESS presses=1 n=1 first_us=41000 assembled_us=58210 decoded_us=63940 shown_us=79120 first_max_us=41000 shown_max_us=79120 frame_bytes=21480 quiet_bytes=1320 order_err=0 expired=0 overlap=0 lost=0 presses_total=14 samples_total=14`

**What it cannot tell you.**
- It needs a quiet screen. Anything else that changes the picture by a big step (a notification, a video playing, an animation, loss recovery that is not flagged corrupt) can be taken for the answer to a press, which gives a false sample. Check `frame_bytes` against `quiet_bytes`.
- Corrupt frames are skipped, so a press answered only by a frame flagged corrupt shows up as `expired`.
- `first_us` is 1 ms coarse; a sample can show `first_us` slightly early, and a press sent in the same millisecond as the first packet can give a small negative value (counted in `order_err`).
- The press time is when the packet was handed to the network, not when the button was touched; the time from the physical press to the send is `PIPE/INPUT`.

### 9.2 `PIPE/DECHOLD`: does the hardware decoder hold pictures back?

**What it measures.** Whether feeding frame N makes the decoder hand back frame N, or an older one (N-1, N-2), and how long each picture sat inside the decoder. Today's code assumes the picture that comes out is the one that just went in; two or three held frames would be a large share of a delay of tens of milliseconds, and nothing else can see it.

**Change to what the decoder is given.** In `decode_frame_now` (`vita/src/video.c`) the access unit's presentation timestamp (`au.pts`) is now the frame's running number (the same number `PIPE/PRESS` uses; `upper` = 0, `lower` = number) instead of `0xFFFFFFFF` in both halves. It is fed so that the number can be read back. `au.dts`, the decoder init parameters and the reference-frame count are unchanged. Before each decode the output picture's `info.pts` is set to `0xFFFFFFFF`, so a decoder that does not write it cannot leave an old number behind. **Whether the Vita decoder echoes the timestamp into `info.pts` is unverified until the first hardware log.** If it does not, `rb_good` stays 0 and `rb_bad` equals the pictures handed back; the frame counts still work on their own.

**Counts.** `fed` counts decodes where `sceAvcdecDecode` returned 0 or more; `handed` is the sum of `numOfOutput`. The hold is `fed - handed`, a running value for the whole session (it does not restart each window).

**Per picture** (only when the read-back is good):
- *Lag in frame numbers* = the number just fed minus the number read back. 0 means the decoder returned the frame it was just given. The numbers are the receive thread's counter, so a frame the decode queue dropped before decoding also shows up as lag; compare with `hold`, which counts only frames the decoder actually received.
- *Hold time* = the clock read taken right after `sceAvcdecDecode` returned minus the clock read taken right before the call that fed the same-numbered frame (both reads already existed for `PIPE/DECODE`). Feed times are kept for the last `DECHOLD_FEED_RING` = 64 frame numbers; a read-back that is no longer in that ring (or was never fed) counts in `rb_stale` and gets no hold time.
- *Good read-back*: `info.pts.upper` is 0, the lower half is not 0 and not `0xFFFFFFFF`, and the number is not ahead of the frame just fed nor more than `DECHOLD_LAG_MAX` = 32 behind it. Anything else counts in `rb_bad`. A decode that returned no picture counts in neither.

**Which frame the press meter uses.** When the read-back is good, `PIPE/PRESS` takes the read-back number as the frame in `frame_texture` (section 9.1); otherwise the fed number.

**Window.** The decode thread closes its own window together with the `decode_avg_us` window (about 1 s, closed only when a picture arrives), reduces it to 32-bit values and publishes them under a sequence lock (`dechold_lock`, odd while writing). The reporter copies them with a few retries and prints the previous copy if none is consistent. The UI thread never resets a counter the decode thread writes. The window the line shows is the decode thread's last closed window, which is not aligned with the reporter's window, so a window can be printed twice or skipped now and then. Reset with the stream (`host_metrics_reset_stream`, `vita_h264_start`).

**Fields of the line.**

| Field | Meaning |
|---|---|
| `fed`, `handed` | Decodes accepted and pictures handed back in the window. |
| `hold` | `fed - handed` over the session, at window close. |
| `hold_min`, `hold_max` | Smallest and largest `hold` seen after any decode in the window. |
| `lag_min`, `lag_avg`, `lag_max` | Lag in frame numbers over good read-backs; average with two decimals; 0 when there were none. |
| `hold_us_avg`, `hold_us_max` | Hold time in microseconds over good, non-stale read-backs; 0 when there were none. |
| `rb_good`, `rb_bad`, `rb_stale` | Read-backs that passed, failed validation, and good ones whose feed time was no longer remembered. |

Example (a decoder holding two frames): `PIPE/DECHOLD fed=60 handed=60 hold=2 hold_min=2 hold_max=2 lag_min=2 lag_avg=2.00 lag_max=2 hold_us_avg=66800 hold_us_max=71200 rb_good=60 rb_bad=0 rb_stale=0`

**What it cannot tell you.**
- It is only as good as the echo: with `rb_good=0` only `hold` is known, and `hold` alone cannot say which frame came out.
- Hold time is measured at the end of the decode call, so it covers time inside the decoder, not the wait before it (decode queue) or after it (waiting for the UI thread).
- A window with no picture at all is not closed (the close runs when a picture arrives), so a total stall shows the last window until output resumes.
- It does not change the decoder's behaviour; it only shows whether a held-frame delay exists, not how to remove it.

### 9.3 `PIPE/RECVBUF`: how much backlog does the receive thread find?

**What it measures.** Video data that sits in the kernel's socket buffer before the receive thread reads it. All other timing starts when the receive thread reads a packet, so a standing backlog there is invisible. At 6 Mbit/s, 20 KB waiting is about 27 ms.

**What shipped, and what did not.** The drain count, the bytes drained and the buffer size. The planned second number, the bytes waiting in the socket (`sceNetGetSockInfo`, field `recv_queue_length`), was **left out** because it cannot be called reliably today:
- `takion->sock` on Vita is a newlib file descriptor, not the SceNet socket id (`socket()` calls `sceNetSocket` and stores the id inside newlib's private descriptor table; every wrapper such as `recvfrom` looks it up there). The VitaSDK headers have no public call to turn the descriptor into the id; it would need newlib's private `__vita_fd_grab` and its undeclared struct layout.
- The meaning of the `n` and `flags` arguments of `sceNetGetSockInfo` is not documented in the VitaSDK headers (`psp2/net/net.h`), and nothing else in this repo calls it.
The unit of `recv_queue_length` (bytes or packets) is also undocumented. If this is picked up later, the sample belongs right after the blocking receive returns and before the drain loop, and the unit must be settled against the drain count from a hardware log.

**Drained packets and bytes per wake-up.** In `takion_thread_func` (`lib/src/takion.c`) the blocking `takion_recv` wakes the thread with one packet; the drain loop then pulls up to `TAKION_RECV_DRAIN_MAX` = 256 more with zero-timeout reads. The number counted for a wake-up is how many the drain loop pulled after the waking packet (the waking packet itself is not counted), taken when the loop ends. A wake-up is one blocking receive that returned a packet. The bytes are the sum of the sizes (`drain_size` after each successful zero-timeout `takion_recv`) of exactly those packets; the waking packet is not included. The probe uses its own counters (`recvbuf_acc`); the existing `jitter_stats.drain_*` counters are left alone because the occasional "Takion jitter:" line resets them.

**Receive buffer size.** `rcvbuf_req` is `takion->a_rwnd` (`TAKION_A_RWND`, 512 KB on Vita) and `rcvbuf_actual` is the value read back from the kernel right after the `SO_RCVBUF` set in both connect paths (the existing `Takion SO_RCVBUF requested=... actual=...` log line is unchanged and is still printed once per connection). `rcvbuf_actual` is 0 if the read-back failed.

**Window.** The receive thread closes its own window with the clock value the loop already reads once per iteration (no extra clock read), once at least `TAKION_RECVBUF_WINDOW_MS` = 1000 have passed. It reduces it to 32-bit values and publishes them under a sequence lock (`recvbuf_lock`, odd while writing), the same way as `PIPE/DECHOLD`. The reporter copies with a few retries and prints the previous copy if none is consistent. The window closes only when a packet arrives, so a total stall shows the last window until packets resume. The UI thread never resets a counter the receive thread writes. The shown window is not aligned with the reporter's, so one can be printed twice or skipped now and then. State is zeroed when the receive thread starts and in `host_metrics_reset_stream`.

**Fields of the line.**

| Field | Meaning |
|---|---|
| `wakeups` | Blocking receives that returned a packet in the window. |
| `drained_avg`, `drained_max` | Average (two decimals) and largest number of packets drained after the waking packet, per wake-up. |
| `drained_bytes_avg`, `drained_bytes_max` | Bytes of the drained packets per wake-up (whole bytes): average and largest. Same packets as `drained_avg` / `drained_max`. |
| `drain_cap` | `TAKION_RECV_DRAIN_MAX`. A `drained_max` equal to it means the cap was hit in that window. |
| `rcvbuf_req`, `rcvbuf_actual` | Receive buffer size asked for and reported by the kernel, in bytes. |

Example: `PIPE/RECVBUF wakeups=410 drained_avg=1.85 drained_max=23 drained_bytes_avg=2650 drained_bytes_max=32400 drain_cap=256 rcvbuf_req=524288 rcvbuf_actual=524288`

**Reading it in milliseconds.** `drained_bytes` times 8, divided by the measured bitrate, is how many milliseconds of stream were waiting at that wake-up (for example 32400 bytes at 6 Mbit/s: 32400 x 8 / 6000000 = 43 ms). It stands in for the kernel's queue length, which is not read (see above). It leans high: packets that arrive while the drain loop runs are counted too, not only those already waiting when the thread woke. It also covers all Takion traffic on the socket (video, audio and control), not video alone. A `drained_max` equal to `drain_cap` means the drain stopped at the cap, so the byte figure of that wake-up is a lower bound. An average near 0 means the thread keeps up and nothing stands behind each waking packet.

**What it cannot tell you.**
- It counts the packets and bytes pulled after the first one, not how long they waited, and it sees only the socket buffer, not the Wi-Fi chip or the console.
- A wake-up that finds the packets one at a time says nothing about whether the socket buffer was deeper between wake-ups.
- No cost of the probe has been measured on hardware; it adds one integer add per drained packet and a few integer updates per wake-up.

### 9.4 `PIPE/CONSOLE`: what the console tells us

**What it measures.** The control messages the console sends over the protobuf channel, which the stream connection mostly throws away. Two questions: what round-trip time does the console itself report (`CONNECTIONQUALITY`, field `rtt`), and which other message types arrive that we do nothing with (in particular `PERIODICTIMESTAMP`, type 27: does the PS5 send it, how often). Logging only: no message is handled differently, nothing is sent, no state outside the probe changes.

**Where each number comes from.** All of it is recorded on the Takion receive thread in `stream_connection_takion_data_idle` (`lib/src/streamconnection.c`), the handler used once the stream is running.
- `CONNECTIONQUALITY` (type 16, `ConnectionQualityPayload`): `rtt`, `target_bitrate`, `upstream_bitrate`, `upstream_loss`, `loss`. The latest value of each is kept, each only when the message carried that field (its `has_` flag). Never sent shows as `-`, not as 0. `rtt_msgs` counts the messages that carried an `rtt`.
- Ignored types: every message that reaches the switch's `default:` is counted by its type number in an array sized from the highest type in `lib/protobuf/takion.proto` (32, `CHIAKI_CONSOLE_INFO_MSG_TYPE_MAX`); a number outside it goes into `other`. The types the switch handles (`DISCONNECT`, `CONNECTIONQUALITY`, `CORRUPTFRAME`, `STREAMINFOACK`) are not counted as ignored. `PERIODICTIMESTAMP` messages are counted both as ignored and in their own `ts_*` figures.
- `PERIODICTIMESTAMP` (type 27): a count, and the time between the last two, from one read of the library's monotonic clock per such message (millisecond resolution). The raw bytes of the first one go to `PIPE/CONSOLE_TS`.

**Hand-off.** The receive thread updates a `ChiakiConsoleInfo` (`lib/include/chiaki/streamconnection.h`) inside a sequence lock (`console_info_lock`, odd while writing), so the 64-bit `rtt` and the other fields are never read half-written. The reporter (`host_metrics_report_console`, `vita/src/host_metrics.c`) copies the whole struct, retries up to 4 times, and prints the previous copy if none was consistent. Counters in the struct are session totals; the per-window figures are the growth since the reporter's previous copy. The reporter writes nothing the receive thread owns. The struct is zeroed in `chiaki_stream_connection_init` (once per session start, as the other per-run counters are) and the reporter's state in `host_metrics_reset_stream`.

**Fields of `PIPE/CONSOLE`** (printed once per window right after `PIPE/RECVBUF`).

| Field | Meaning |
|---|---|
| `quality_win`, `quality_total` | `CONNECTIONQUALITY` messages in the window and in the session. |
| `rtt_msgs_win` | Of the window's messages, how many carried an `rtt`. |
| `rtt_range_n` | How many values `rtt_min` / `rtt_max` cover: the window's, but at most the newest 16 (`CHIAKI_CONSOLE_INFO_RTT_RING`). Smaller than `rtt_msgs_win` only if more than 16 arrived in one window. |
| `rtt`, `rtt_min`, `rtt_max` | Latest `rtt` the console sent, and the smallest and largest in the window. Six decimals. `-` if none yet / none in the window. **Unit unverified** (the proto only says `double`). |
| `target_bitrate`, `upstream_bitrate`, `upstream_loss`, `loss` | Latest values the console sent, as sent; `-` if never sent. Units as in the proto; not checked against hardware. |
| `ignored_win`, `ignored_total` | Messages that reached the `default:` case in the window and in the session. |
| `types=[type:count,...]` | Session totals per ignored type number, for every type seen at least once; `other:` for a number out of range. Cut off with `...` if the text does not fit in 256 characters. `[]` if none. |
| `ts_win`, `ts_total` | `PERIODICTIMESTAMP` messages in the window and in the session. |
| `ts_gap_ms` | Milliseconds between the last two `PERIODICTIMESTAMP` messages; `-` before the second one. |

Example: `PIPE/CONSOLE quality_win=1 quality_total=84 rtt_msgs_win=1 rtt_range_n=1 rtt=0.018250 rtt_min=0.018250 rtt_max=0.018250 target_bitrate=6000 upstream_bitrate=0 upstream_loss=0.0000 loss=0 ignored_win=3 ignored_total=251 types=[27:84,3:167] ts_win=1 ts_total=84 ts_gap_ms=1002` (all numbers made up).

**`PIPE/CONSOLE_TS`.** Printed once per session, from the UI thread, the first window after the first `PERIODICTIMESTAMP` message was seen: `PIPE/CONSOLE_TS first len=<n> hex=<bytes>`. `len` is the true length of that whole protobuf message; `hex` is its first 64 bytes (`CHIAKI_CONSOLE_INFO_FIRST_TS_CAP`), and ` truncated_to=64` is appended if the message was longer. It is the raw message including the type field, not decoded: `periodic_ts_info` is a callback-typed bytes field in nanopb and the decoding is left untouched. Example (made up): `PIPE/CONSOLE_TS first len=12 hex=081bda01080a0400000000`.

**Handlers before the stream runs.** The expect-bang and expect-streaminfo handlers do not drop other types silently: a message of the wrong type is logged at error level (`expected bang payload but received something else: <type>` and the streaminfo equivalent) and returned from, apart from `DISCONNECT`, which is handled. They are not instrumented, because a periodic timestamp arriving there would already be logged.

**What it cannot tell you.**
- `rtt` is the console's estimate, not a measurement of ours, and its unit is unverified until the first hardware log. Do not convert it before then.
- A type in the `types` list only means we do nothing with it; it says nothing about what the message contains.
- A message that fails to decode is logged as an error by the existing code and not counted here.
- The lines print only with `show_latency` on, like the other probes. Window figures are not aligned with the console's cadence, so a message can appear in a neighbouring window.
- No cost of the probe has been measured on hardware; per message it adds a few integer updates and, for a periodic timestamp, one clock read and one copy of at most 64 bytes (once).

## 10. What is not here

- No RTT or jitter figures are quoted in this document on purpose. The two readouts in section 2 are explained there; the console's own estimate in `PIPE/CONSOLE` (section 9.4) has an unverified unit and no value is quoted for it.
- PSN (internet) connection setup is in [psn.md](psn.md).
