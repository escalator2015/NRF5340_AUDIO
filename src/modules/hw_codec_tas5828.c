/*
 * hw_codec implementation para TAS5828 (drop-in del CS47L63).
 * Implementa la API de src/modules/hw_codec.h.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "hw_codec.h"
#include "tas5828.h"
#include "zbus_common.h"
#include "macros_common.h"

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hw_codec_tas5828, CONFIG_HW_CODEC_TAS5828_LOG_LEVEL);

/* Volumen actual en dB (0 = máximo, -64 = mínimo útil) */
static int8_t current_vol_db = CONFIG_HW_CODEC_TAS5828_DEFAULT_VOL_DB;
static bool   current_muted;

/* --- API hw_codec.h ---------------------------------------------------- */

int hw_codec_volume_set(uint8_t set_val)
{
	/* set_val: rango 0-128 (igual que el CS47L63).
	 * Conversión: db = (set_val / 2) - 64
	 * 128 → 0dB, 0 → -64dB
	 */
	int8_t db = (int8_t)((int16_t)set_val / 2 - 64);

	current_vol_db = db;
	current_muted = false;
	return tas5828_volume_set_db(db);
}

int hw_codec_volume_adjust(int8_t adjustment)
{
	if (adjustment == 0) {
		/* Re-aplicar valor conocido */
		return tas5828_volume_set_db(current_vol_db);
	}

	int16_t new_db = (int16_t)current_vol_db + adjustment;

	if (new_db > 0) {
		new_db = 0;
	} else if (new_db < -64) {
		new_db = -64;
	}

	current_vol_db = (int8_t)new_db;
	current_muted = false;
	return tas5828_volume_set_db(current_vol_db);
}

int hw_codec_volume_decrease(void)
{
	return hw_codec_volume_adjust(-3);
}

int hw_codec_volume_increase(void)
{
	return hw_codec_volume_adjust(3);
}

int hw_codec_volume_mute(void)
{
	current_muted = true;
	return tas5828_mute(true);
}

int hw_codec_volume_unmute(void)
{
	current_muted = false;
	return tas5828_mute(false);
}

int hw_codec_default_conf_enable(void)
{
	/* No hay configuración extra necesaria en el TAS5828 para I2S puro */
	return 0;
}

int hw_codec_soft_reset(void)
{
	/* No implementado: el TAS5828 se resetea con su pin nRESET o power-cycle */
	return 0;
}

int hw_codec_init(void)
{
	int ret;

	ret = tas5828_init();
	if (ret) {
		LOG_ERR("TAS5828 init failed: %d", ret);
		return ret;
	}

	/* Aplicar volumen y mute por defecto */
	ret = tas5828_volume_set_db(current_vol_db);
	if (ret) {
		return ret;
	}

	ret = tas5828_mute(current_muted);
	if (ret) {
		return ret;
	}

	LOG_INF("hw_codec TAS5828 ready, vol=%ddB muted=%d",
		current_vol_db, current_muted);
	return 0;
}

/* --- Subscriber zbus para VOLUME_* ------------------------------------- */

ZBUS_SUBSCRIBER_DEFINE(volume_evt_sub, CONFIG_HW_CODEC_TAS5828_VOLUME_SUB_QUEUE_SIZE);

ZBUS_CHAN_DECLARE(volume_chan);

static void volume_msg_sub_thread(void)
{
	int ret;
	const struct zbus_channel *chan;

	while (1) {
		ret = zbus_sub_wait(&volume_evt_sub, &chan, K_FOREVER);
		ERR_CHK(ret);

		struct volume_msg msg;

		ret = zbus_chan_read(chan, &msg, ZBUS_READ_TIMEOUT_MS);
		ERR_CHK(ret);

		switch (msg.event) {
		case VOLUME_UP:
			ret = hw_codec_volume_increase();
			if (ret) {
				LOG_WRN("Volume increase failed: %d", ret);
			}
			break;

		case VOLUME_DOWN:
			ret = hw_codec_volume_decrease();
			if (ret) {
				LOG_WRN("Volume decrease failed: %d", ret);
			}
			break;

		case VOLUME_SET:
			ret = hw_codec_volume_set(msg.volume);
			if (ret) {
				LOG_WRN("Volume set failed: %d", ret);
			}
			break;

		case VOLUME_MUTE:
			ret = hw_codec_volume_mute();
			if (ret) {
				LOG_WRN("Volume mute failed: %d", ret);
			}
			break;

		case VOLUME_UNMUTE:
			ret = hw_codec_volume_unmute();
			if (ret) {
				LOG_WRN("Volume unmute failed: %d", ret);
			}
			break;

		default:
			LOG_WRN("Unexpected volume event: %d", msg.event);
			break;
		}
	}
}

K_THREAD_DEFINE(volume_msg_sub_thread_id, CONFIG_HW_CODEC_TAS5828_THREAD_STACK_SIZE,
		volume_msg_sub_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_HW_CODEC_TAS5828_THREAD_PRIO), 0, 0);
