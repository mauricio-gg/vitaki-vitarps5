// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/congestioncontrol.h>

#define CONGESTION_CONTROL_INTERVAL_MS 200

/* Cap the loss ratio reported to the PS5. The PS5's adaptive bitrate keys on
 * this value; uncapped, pre-FEC burst spikes (e.g. 22/85 = 26%) walk its
 * encoder down from 6000 to ~3418 kbps, collapsing the stream to ~1 Mbps.
 * Clamping at 10% matches chiaki-ng's approach. */
#define CONGESTION_MAX_REPORTED_LOSS 0.10

/* Fraction of FEC-recovered units still reported as lost so the PS5 backs off
 * when FEC is heavily exercised. Integer division gives a natural deadband:
 * light recoverable loss (1-2 units / 200ms) reports 0. */
#define CONGESTION_FEC_RECOVERED_LOSS_DIVISOR 4
#define CONGESTION_LOG_INTERVAL_TICKS 5 /* 5 x 200ms = 1s */

static void *congestion_control_thread_func(void *user)
{
	ChiakiCongestionControl *control = user;

	ChiakiErrorCode err = chiaki_bool_pred_cond_lock(&control->stop_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return NULL;

	while(true)
	{
		err = chiaki_bool_pred_cond_timedwait(&control->stop_cond, CONGESTION_CONTROL_INTERVAL_MS);
		if(err != CHIAKI_ERR_TIMEOUT)
			break;

		if(!control->takion || !control->stats || !control->log)
			continue;

		uint64_t received = 0;
		uint64_t lost = 0;
		uint64_t recovered = 0;
		chiaki_packet_stats_get(control->stats, true, &received, &lost, &recovered);

		/* Discount FEC-recovered units into the reported loss (deadbanded by the
		 * divisor) so the PS5 still backs off when FEC is heavily exercised, even
		 * though the recovered units themselves aren't raw loss. */
		uint64_t reported_lost = lost + recovered / CONGESTION_FEC_RECOVERED_LOSS_DIVISOR;

		/* Clamp reported loss ratio to CONGESTION_MAX_REPORTED_LOSS so burst
		 * spikes don't cause the PS5 to over-throttle its encoder. */
		uint64_t raw_reported_lost = reported_lost;
		uint64_t total = received + reported_lost;
		if(total > 0)
		{
			double loss_ratio = (double)reported_lost / (double)total;
			if(loss_ratio > CONGESTION_MAX_REPORTED_LOSS)
			{
				reported_lost = (uint64_t)(((double)received * CONGESTION_MAX_REPORTED_LOSS)
					/ (1.0 - CONGESTION_MAX_REPORTED_LOSS));
			}
		}

		ChiakiTakionCongestionPacket packet = { 0 };
		packet.received = (uint16_t)received;
		packet.lost = (uint16_t)reported_lost;
		if(raw_reported_lost != reported_lost)
			CHIAKI_LOGV(control->log,
				"Sending Congestion Control Packet, received: %u, lost: %u (capped from %u)",
				(unsigned int)packet.received, (unsigned int)packet.lost, (unsigned int)raw_reported_lost);
		else
			CHIAKI_LOGV(control->log, "Sending Congestion Control Packet, received: %u, lost: %u",
				(unsigned int)packet.received, (unsigned int)packet.lost);
		chiaki_takion_send_congestion(control->takion, &packet);

		/* 1Hz aggregate CONGESTION/LOSS log: the per-tick CHIAKI_LOGV above is too
		 * noisy for routine visibility, so accumulate across CONGESTION_LOG_INTERVAL_TICKS
		 * ticks and emit one CHIAKI_LOGD summary per second. Includes the pre-cap
		 * post-FEC-discount value alongside the post-cap value actually sent, so
		 * cap activation (precap != reported) is visible in the aggregate too,
		 * not just in the per-tick CHIAKI_LOGV "(capped from ...)" line. */
		control->log_accum_received += received;
		control->log_accum_raw_lost += lost;
		control->log_accum_recovered += recovered;
		control->log_accum_reported_lost_precap += raw_reported_lost;
		control->log_accum_reported_lost += reported_lost;
		control->log_accum_ticks++;
		if(control->log_accum_ticks >= CONGESTION_LOG_INTERVAL_TICKS)
		{
			CHIAKI_LOGD(control->log,
				"CONGESTION/LOSS received=%llu raw_lost=%llu fec_recovered=%llu reported_precap=%llu reported=%llu",
				(unsigned long long)control->log_accum_received,
				(unsigned long long)control->log_accum_raw_lost,
				(unsigned long long)control->log_accum_recovered,
				(unsigned long long)control->log_accum_reported_lost_precap,
				(unsigned long long)control->log_accum_reported_lost);
			control->log_accum_received = 0;
			control->log_accum_raw_lost = 0;
			control->log_accum_recovered = 0;
			control->log_accum_reported_lost_precap = 0;
			control->log_accum_reported_lost = 0;
			control->log_accum_ticks = 0;
		}
	}

	chiaki_bool_pred_cond_unlock(&control->stop_cond);
	return NULL;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_congestion_control_start(ChiakiCongestionControl *control, ChiakiTakion *takion, ChiakiPacketStats *stats, ChiakiLog *log)
{
	control->takion = takion;
	control->stats = stats;
	control->log = log;

#ifdef VITARPS5_CONGESTION_PARITY_INCLUSIVE_RECEIVED
	CHIAKI_LOGI(log, "CONGESTION/RECEIVED_MODE parity_inclusive=1 (GH #221 A/B: FEC parity units counted toward reported congestion 'received')");
#else
	CHIAKI_LOGI(log, "CONGESTION/RECEIVED_MODE parity_inclusive=0 (GH #221 A/B: source units only, baseline since PR #213)");
#endif

	control->log_accum_received = 0;
	control->log_accum_raw_lost = 0;
	control->log_accum_recovered = 0;
	control->log_accum_reported_lost_precap = 0;
	control->log_accum_reported_lost = 0;
	control->log_accum_ticks = 0;

	ChiakiErrorCode err = chiaki_bool_pred_cond_init(&control->stop_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_thread_create(&control->thread, congestion_control_thread_func, control);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_bool_pred_cond_fini(&control->stop_cond);
		return err;
	}

	chiaki_thread_set_name(&control->thread, "Chiaki Congestion Control");

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_congestion_control_stop(ChiakiCongestionControl *control)
{
	ChiakiErrorCode err = chiaki_bool_pred_cond_signal(&control->stop_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_thread_join(&control->thread, NULL);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	// FIXME ywnico check what to set thread_id to
	#ifdef __PSVITA__
	control->thread.thread_id = 0;
	#else
	control->thread.thread = 0;
	#endif

	return chiaki_bool_pred_cond_fini(&control->stop_cond);
}
