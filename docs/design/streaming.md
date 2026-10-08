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
- **The "jitter" number is not network jitter.** `takion->jitter_stats.jitter_us` is an EWMA (weight 1/8) of how much the gap between consecutive control-channel packets changes (`takion.c:2171-2176`). Video and audio never touch it. Video has its own, separate figure (`video_jitter_stats`, `takion.c:2454`). The log line starting `Latency metrics` prints `senkusha_rtt` (a single round trip measured during the handshake, never updated afterwards, `host_metrics.c:712-717`) and `ctrl_cadence_jitter` (that control-channel EWMA); the line itself says "NOT real network jitter". Neither is a live round trip.

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
4. It cannot join its own thread, so it sets `session_finalize_pending = true` and `session_init = false` and returns (`host_quit.c:139-143`). The UI thread does the join and `chiaki_session_fini` in `host_finalize_deferred_session` (`vita/src/host_lifecycle.c:63`), called from the top of the UI loop (`vita/src/ui.c:499-501`).
5. It stores the schedule: `loss_retry_ready_us`, the attempt count, the cause string, and sets `loss_retry_pending = true` after a memory barrier so the UI thread can never see the pending flag before the finalize flag (`host_quit.c:341-385`, barrier at `384`).

### 8.3 The UI loop starts the reconnect

In `vita/src/ui.c:545-565` the UI thread starts the new connection when all of these hold: `recovery_active`, `loss_retry_pending`, the old session is finalized (`!session_finalize_pending && !session_init`), no connection thread is running, and the clock has passed `loss_retry_ready_us`. If there is no active host or the connection thread cannot start, it calls `host_recovery_abort` (`host_quit.c:418`), which clears all recovery state and shows "Could not reconnect to console". The screen is the Reconnecting screen for the whole episode (`ui.c:647-650`).

The reconnect always uses the normal preset bitrate from `host_default_video_profile` (`host_quit.c:92-95`, `368`; `host.c:193`, `310-318`), never the dying session's bitrate.

### 8.4 State that survives `host_stream()`

`recovery_active`, the attempt count, the release-poll count and the overlay start time are not reset when `host_stream()` starts a new connect (`host.c:278-279` reads them). They are cleared only when the new session reports `CHIAKI_EVENT_CONNECTED` (`host_callbacks.c:59-74`, "Recovery complete"), on abort, on exhaustion, or on user cancel. The overlay itself is cleared on the first video frame (`host_callbacks.c:114-122`). A later drop after a successful recovery therefore starts a new episode with the full budget of 2.

### 8.5 Timing: the first attempt and the console-release polls (PR #279)

- **First attempt after a transport death.** `ready_us = now + RECOVERY_CONSOLE_RELEASE_DELAY_US`, which is `RETRY_HOLDOFF_RP_IN_USE_MS` = 9000 ms (`host_quit.c:19`, `24`, `341`). The delay is still 9 s here; PR #279 did not change it. (A debug-widget resync skips it: `ready_us = now`, `host_quit.c:342-343`.)
- **If the console still refuses (RP_IN_USE) during recovery.** Instead of waiting out another fixed 9 s, the handler sets `release_poll` (`host_quit.c:114-115`) and schedules the next probe `RECOVERY_RELEASE_POLL_INTERVAL_US` = 1 s later (`host_quit.c:31`, `194-196`, `344-345`). The code comment (`host_quit.c:25-30`) notes a refused probe itself costs about 250 ms. Up to `RECOVERY_RELEASE_POLL_MAX` = 8 polls (`host_quit.c:32`). A poll does not use up the attempt budget and the Reconnecting screen's "Attempt N" does not change (`host_quit.c:364-366`). Each poll logs `Release wait: RP_IN_USE poll n/8 ...` (`host_quit.c:350-355`).
- **After 8 polls.** The next refusal falls back to the old behaviour: it counts as an attempt, logs "polls exhausted ... falling back to 9000 ms holdoff" (`host_quit.c:356-361`), and the holdoff of 9000 ms is armed (`host_quit.c:199-209`). When attempts reach 2, recovery is exhausted: `recovery_exhausted` shows the "Could not reconnect to console" banner and hint and the app lands on the main menu (`host_quit.c:299-306`, `386-395`).
- **Cancel.** Circle on the Reconnecting screen calls `host_recovery_cancel_by_user` (`vita/src/ui/ui_screens.c:3825`, `host_quit.c:426`). It clears all recovery state and requests a stop; a connect that has not reached `session_init` is cancelled inside `host_stream()` by the barrier check at `host.c:521-527`.

### 8.6 RP_IN_USE outside a recovery (PR #215 path)

When the console answers a plain user connect with RP_IN_USE, `arm_rp_in_use_auto_retry` (`host_quit.c:176-178`) arms one automatic retry: `rp_in_use_retry_at_us` = now + `RP_IN_USE_AUTO_RETRY_DELAY_US` (6 s, `host_quit.c:36`), or later if a cooldown is still running (`host_quit.c:266-268`). `rp_in_use_retry_used` makes it fire once. The UI loop starts it only when the screen is the main menu and no connection is in flight, and it restores the user's PSN-versus-LAN choice (`ui.c:517-532`). Inside a recovery this path is off (`!restart_context`) because the recovery logic above handles RP_IN_USE.

The post-stop guard applies when the user ends a stream: after a clean disconnect the console acknowledged, a new connect is blocked for `POST_STOP_GUARD_DISCONNECT_ACKED_US` = 2 s; sent but not acknowledged, or never sent after the stream was live, 8 s (`POST_STOP_GUARD_DISCONNECT_UNACKED_US`); a connect cancelled before any stream started has no guard (`host_quit.c:34-35`, `229-258`).

## 9. What is not here

- No RTT or jitter figures are quoted in this document on purpose; the only two such readouts in the logs are explained in section 2.
- PSN (internet) connection setup is in [psn.md](psn.md).
