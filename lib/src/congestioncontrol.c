// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/congestioncontrol.h>

#define CONGESTION_CONTROL_INTERVAL_MS 200

/* Cap the loss ratio reported to the PS5. The PS5's adaptive bitrate keys on
 * this value; uncapped, pre-FEC burst spikes (e.g. 22/85 = 26%) walk its
 * encoder down from 6000 to ~3418 kbps, collapsing the stream to ~1 Mbps.
 * Clamping at 10% matches chiaki-ng's approach. */
#define CONGESTION_MAX_REPORTED_LOSS 0.10

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
		chiaki_packet_stats_get(control->stats, true, &received, &lost);

		/* Clamp reported loss ratio to CONGESTION_MAX_REPORTED_LOSS so burst
		 * spikes don't cause the PS5 to over-throttle its encoder.
		 * Re-derive lost from received so the ratio is exactly the cap. */
		uint64_t total = received + lost;
		uint64_t raw_lost = lost;
		if(total > 0)
		{
			double loss_ratio = (double)lost / (double)total;
			if(loss_ratio > CONGESTION_MAX_REPORTED_LOSS)
			{
				lost = (uint64_t)(((double)received * CONGESTION_MAX_REPORTED_LOSS)
					/ (1.0 - CONGESTION_MAX_REPORTED_LOSS));
				total = received + lost;
			}
		}

		ChiakiTakionCongestionPacket packet = { 0 };
		packet.received = (uint16_t)received;
		packet.lost = (uint16_t)lost;
		control->packet_loss = total > 0 ? (double)lost / total : 0;
		if(raw_lost != lost)
			CHIAKI_LOGV(control->log,
				"Sending Congestion Control Packet, received: %u, lost: %u (capped from %u)",
				(unsigned int)packet.received, (unsigned int)packet.lost, (unsigned int)raw_lost);
		else
			CHIAKI_LOGV(control->log, "Sending Congestion Control Packet, received: %u, lost: %u",
				(unsigned int)packet.received, (unsigned int)packet.lost);
		chiaki_takion_send_congestion(control->takion, &packet);
	}

	chiaki_bool_pred_cond_unlock(&control->stop_cond);
	return NULL;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_congestion_control_start(ChiakiCongestionControl *control, ChiakiTakion *takion, ChiakiPacketStats *stats, ChiakiLog *log)
{
	control->takion = takion;
	control->stats = stats;
	control->log = log;
	control->packet_loss = 0;

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
