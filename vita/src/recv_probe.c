// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "recv_probe.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>

#include "audio.h"
#include "context.h"

/* q_avg of PIPE/AUDIO is printed with two decimals. */
#define AUDIO_Q_AVG_SCALE 100u

/* What PIPE/RECVTHREAD printed last this stream; the line repeats only when one of these moves,
 * or when audio has started or stopped since (the confirmation that the thread stayed put). */
static struct {
  bool printed;
  int32_t prio;
  int32_t affinity;
  int32_t cpu;
  uint32_t audio_started;
} thread_printed;

ChiakiTakionRecvTimeAcc *recv_probe_acc(void) {
  if (!context.stream.session_init)
    return NULL;
  ChiakiTakion *takion = &context.stream.session.stream_connection.takion;
  if (sceKernelGetThreadId() != takion->recvtime_thread_id)
    return NULL;
  return &takion->recvtime_acc;
}

RecvProbeLog recv_probe_log_begin_at(uint64_t start_us) {
  RecvProbeLog log = {recv_probe_acc(), start_us};
  return log;
}

RecvProbeLog recv_probe_log_begin(void) {
  RecvProbeLog log = {recv_probe_acc(), 0};
  if (log.acc)
    log.start_us = sceKernelGetProcessTimeWide();
  return log;
}

void recv_probe_log_end(RecvProbeLog log, ChiakiLogLevel level) {
  if (!log.acc || !vita_log_should_write_level(level))
    return;
  uint64_t took_us = sceKernelGetProcessTimeWide() - log.start_us;
  ChiakiTakionRecvTimeAcc *acc = log.acc;
  chiaki_takion_time_bucket_add(&acc->log, took_us);
  switch (acc->cur_class) {
    case CHIAKI_TAKION_PROBE_CLASS_VIDEO:
      acc->vlog_us += took_us;
      break;
    case CHIAKI_TAKION_PROBE_CLASS_AUDIO:
      acc->alog_us += took_us;
      break;
    case CHIAKI_TAKION_PROBE_CLASS_CONTROL:
      acc->clog_us += took_us;
      break;
    default:
      break;
  }
  /* vita_log_submit_line() takes the blocking mutex path for every level except DEBUG. */
  if (level != CHIAKI_LOG_DEBUG)
    acc->log_block++;
}

void recv_probe_reset_report(void) {
  memset(&thread_printed, 0, sizeof(thread_printed));
}

void recv_probe_report(const ChiakiTakionRecvbufWindow *w, const AudioOutWindow *ao) {
  LOGD(
      "PIPE/RECVTIME video_n=%u video_us=%u video_max_us=%u audio_n=%u audio_us=%u "
      "audio_max_us=%u ctrl_n=%u ctrl_us=%u ctrl_max_us=%u vsubmit_n=%u vsubmit_us=%u "
      "vsubmit_max_us=%u acb_n=%u acb_us=%u acb_max_us=%u acatchup=%u work_us=%u "
      "unacct_us=%lld log_n=%u log_us=%u log_max_us=%u vlog_us=%u alog_us=%u clog_us=%u "
      "log_block=%u",
      w->video.n, w->video.us, w->video.max_us, w->audio.n, w->audio.us, w->audio.max_us, w->ctrl.n,
      w->ctrl.us, w->ctrl.max_us, w->vsubmit.n, w->vsubmit.us, w->vsubmit.max_us, w->acb.n,
      w->acb.us, w->acb.max_us, w->acatchup, w->work_us, (long long)w->unacct_us, w->log.n,
      w->log.us, w->log.max_us, w->vlog_us, w->alog_us, w->clog_us, w->log_block);
  const ChiakiTakionAudioStats *a = &w->audio_stats;
  uint32_t q_avg_x100 = chiaki_takion_probe_avg((uint64_t)a->q_sum * AUDIO_Q_AVG_SCALE, a->q_n);
  LOGD(
      "PIPE/AUDIO net_pkts=%u haptic_pkts=%u frames_new=%u frames_dup=%u frames_missing=%u "
      "gap_events=%u decoded=%u catchups=%u catchup_frames=%u q_min=%u q_avg=%u.%02u q_max=%u",
      a->net_pkts, a->haptic_pkts, a->frames_new, a->frames_dup, a->frames_missing, a->gap_events,
      a->decoded, w->acatchup, a->catchup_frames, a->q_n ? a->q_min : 0,
      q_avg_x100 / AUDIO_Q_AVG_SCALE, q_avg_x100 % AUDIO_Q_AVG_SCALE, a->q_max);
  LOGD(
      "PIPE/AUDIOOUT win=%u out_calls=%u out_frames=%u aout_us=%u aout_max_us=%u rest_n=%u "
      "rest_err=%u "
      "rest_min=%d rest_max=%d out_dry=%u out_gap_max_us=%u out_late=%u waits=%u wall_us=%u",
      ao->win, ao->out_calls, ao->out_frames, ao->aout_us, ao->aout_max_us, ao->rest_n,
      ao->rest_err, ao->rest_min, ao->rest_max, ao->out_dry, ao->out_gap_max_us, ao->out_late,
      ao->waits, ao->wall_us);
  LOGD(
      "PIPE/SOCKQ ok=%u n=%u min=%u avg=%u max=%u wake_avg=%u wake_max=%u query_us=%u "
      "query_max_us=%u err=%u",
      w->session.sockq_ok, w->sockq_n, w->sockq_min, w->sockq_avg, w->sockq_max, w->sockq_wake_avg,
      w->sockq_wake_max, w->sockq_query.us, w->sockq_query.max_us, w->sockq_err);

  const ChiakiTakionRecvSession *s = &w->session;
  const ChiakiTakionRecvThread *t = &w->thread;
  if (!s->valid)
    return;
  uint32_t audio_started = vita_audio_started() ? 1u : 0u;
  if (thread_printed.printed && thread_printed.prio == t->prio &&
      thread_printed.affinity == t->affinity && thread_printed.cpu == t->cpu &&
      thread_printed.audio_started == audio_started)
    return;
  thread_printed.printed = true;
  thread_printed.prio = t->prio;
  thread_printed.affinity = t->affinity;
  thread_printed.cpu = t->cpu;
  thread_printed.audio_started = audio_started;
  const ChiakiTakionRecvThread *st = &s->start;
  LOGD(
      "PIPE/RECVTHREAD prio=%d affinity=0x%x cpu=%d last_cpu=%d info_rc=%d start_prio=%d "
      "start_affinity=0x%x start_cpu=%d start_info_rc=%d audio_started=%u clock_read_ns=%u "
      "thread_id_ns=%u sockq_ok=%u sockq_flags=%d sockq_id=%d sockq_info_rc=%d "
      "sockq_info_rc_self=%d sockq_info_id=%d sockq_info_port_raw=%u sockname_rc=%d "
      "sockname_port_net=%u sockname_port_host=%u",
      t->prio, (unsigned int)t->affinity, t->cpu, t->last_cpu, t->info_rc, st->prio,
      (unsigned int)st->affinity, st->cpu, st->info_rc, audio_started, s->clock_read_ns,
      s->thread_id_ns, s->sockq_ok, s->sockq_flags, s->sockq_id, s->sockq_info_rc,
      s->sockq_info_rc_self, s->sockq_info_id, s->sockq_info_port_raw, s->sockname_rc,
      s->sockname_port_net, s->sockname_port_host);
}
