/*
 * hw_codec implementation para TAS5828 (drop-in del CS47L63).
 * Implementa la API de src/modules/hw_codec.h.
 *
 * Deteción automática en tiempo de ejecución:
 *   - Si el TAS5828 responde al scan I2C inicial → control HW (I2C).
 *   - Si no responde (sink de solo I2S) → control SW (escalado PCM).
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "hw_codec.h"
#include "tas5828.h"
#include "zbus_common.h"
#include "macros_common.h"

#include <stdint.h>
#include <math.h>              /* powf */
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/util.h>   /* CLAMP, MIN */
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hw_codec_tas5828, CONFIG_HW_CODEC_TAS5828_LOG_LEVEL);

/* ---------- Estado común ------------------------------------------------ */

static bool tas5828_present; /* true si detectó por I2C en hw_codec_init() */

/* Volumen en dB para la ruta HW */
static int8_t hw_vol_db   = CONFIG_HW_CODEC_TAS5828_DEFAULT_VOL_DB;
static bool   hw_muted;

/*
 * Gain SW en formato Q16 (0 – 65 536):
 *   65 536 = ganancia 1.0 = 0 dB (volúmen máximo)
 *       0 = silencio
 *
 * Escala lineal mapeada desde el rango 0-128 de hw_codec_volume_set():
 *   set_val=128 → sw_gain_q16=65 536 (0 dB)
 *   set_val=64  → sw_gain_q16=32 768 (-6 dB aprox.)
 *   set_val=0   → sw_gain_q16=0    (silencio)
 */
static uint32_t sw_gain_q16 = 65536u; /* volúmen máximo al arrancar */
static bool     sw_muted;
static uint32_t sw_gain_before_mute = 65536u;

/* ---------- Ayudas internas -------------------------------------------- */

/*
 * Convierte hw_val (0-128) a ganancia Q16 (0-65536) con escala dB correcta.
 *
 * hw_val=128 → dB=  0 → gain=1.000 → Q16=65536
 * hw_val= 98 → dB=-15 → gain=0.178 → Q16=11654
 * hw_val= 68 → dB=-30 → gain=0.032 → Q16= 2072
 * hw_val=  0 → silencio → Q16=0
 *
 * Relación: dB = hw_val/2 - 64  →  gain = 10^(dB/20)
 */
static uint32_t set_val_to_q16(uint8_t set_val)
{
	if (set_val == 0) {
		return 0u;
	}
	float db   = (float)set_val * 0.5f - 64.0f;
	float gain = powf(10.0f, db / 20.0f);
	uint32_t q16 = (uint32_t)(gain * 65536.0f + 0.5f);

	return MIN(q16, 65536u);
}

/*
 * Aplica +3 dB o -3 dB sobre el gain Q16 actual.
 * ±3 dB ≡ factor ×1.413 / ×0.708 en lineal.
 * Aproximación entera:
 *   +3 dB: gain = (gain * 92682) >> 16   (92682/65536 ≈ 1.414)
 *   -3 dB: gain = (gain * 46341) >> 16   (46341/65536 ≈ 0.707)
 */
static void sw_gain_step(int8_t dir)
{
	if (dir > 0) {
		sw_gain_q16 = MIN(65536u, (sw_gain_q16 * 92682u) >> 16);
	} else {
		sw_gain_q16 = (sw_gain_q16 * 46341u) >> 16;
	}
}

/* ---------- API hw_codec.h --------------------------------------------- */

int hw_codec_volume_set(uint8_t set_val)
{
	if (tas5828_present) {
		/* set_val rango 0-128: mapeo a dB: db = set_val/2 - 64 */
		hw_vol_db = (int8_t)((int16_t)set_val / 2 - 64);
		hw_muted = false;
		return tas5828_volume_set_db(hw_vol_db);
	}

	/* Ruta SW */
	sw_gain_q16 = set_val_to_q16(set_val);
	sw_muted = false;
	LOG_DBG("SW vol set %u (Q16=%u)", set_val, sw_gain_q16);
	return 0;
}

int hw_codec_volume_adjust(int8_t adjustment)
{
	if (tas5828_present) {
		if (adjustment == 0) {
			return tas5828_volume_set_db(hw_vol_db);
		}
		int16_t new_db = (int16_t)hw_vol_db + adjustment;

		new_db = CLAMP(new_db, -64, 0);
		hw_vol_db = (int8_t)new_db;
		hw_muted = false;
		return tas5828_volume_set_db(hw_vol_db);
	}

	/* Ruta SW */
	if (adjustment > 0) {
		for (int i = 0; i < adjustment / 3; i++) {
			sw_gain_step(1);
		}
	} else {
		for (int i = 0; i < (-adjustment) / 3; i++) {
			sw_gain_step(-1);
		}
	}
	sw_muted = false;
	LOG_DBG("SW vol adjust %d → Q16=%u", adjustment, sw_gain_q16);
	return 0;
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
	if (tas5828_present) {
		hw_muted = true;
		return tas5828_mute(true);
	}

	/* Ruta SW */
	if (!sw_muted) {
		sw_gain_before_mute = sw_gain_q16;
		sw_gain_q16 = 0u;
		sw_muted = true;
		LOG_DBG("SW mute");
	}
	return 0;
}

int hw_codec_volume_unmute(void)
{
	if (tas5828_present) {
		hw_muted = false;
		return tas5828_mute(false);
	}

	/* Ruta SW */
	if (sw_muted) {
		sw_gain_q16 = sw_gain_before_mute;
		sw_muted = false;
		LOG_DBG("SW unmute");
	}
	return 0;
}

int hw_codec_default_conf_enable(void)
{
	/* Sin configuración extra para I2S puro */
	return 0;
}

int hw_codec_soft_reset(void)
{
	/* El TAS5828 se resetea con nRESET o power-cycle */
	return 0;
}

int hw_codec_init(void)
{
	int ret;

	/* ---- Scan I2C: detectar si hay TAS5828 físicamente ---- */
	ret = tas5828_probe();
	if (ret == 0) {
		/* TAS5828 presente: iniciálisarlo vía I2C */
		tas5828_present = true;

		ret = tas5828_init();
		if (ret) {
			LOG_ERR("TAS5828 init I2C fallido: %d", ret);
			return ret;
		}

		ret = tas5828_volume_set_db(hw_vol_db);
		if (ret) {
			return ret;
		}

		ret = tas5828_mute(hw_muted);
		if (ret) {
			return ret;
		}

		LOG_INF("hw_codec: TAS5828 HW (I2C), vol=%ddB muted=%d",
			hw_vol_db, hw_muted);
	} else {
		/* Sin TAS5828: desactivar I2C y usar control SW */
		tas5828_present = false;
		sw_gain_q16 = 65536u; /* volúmen máximo */
		sw_muted    = false;

		LOG_INF("hw_codec: TAS5828 ausente → control de volumen por SOFTWARE (PCM scaling)");
	}

	return 0;
}

/* ---------- Aplicación de gain SW sobre buffer PCM -------------------- */

void hw_codec_sw_vol_apply(void *buf, uint32_t num_samps)
{
	/* Si hay amplificador HW, el nivel lo controla el TAS5828 — no hacer nada */
	if (tas5828_present) {
		return;
	}

	uint32_t gain = sw_muted ? 0u : sw_gain_q16;

	if (gain == 65536u) {
		/* Volumen máximo: no modificar muestras */
		return;
	}

#if CONFIG_AUDIO_BIT_DEPTH_OCTETS == 2
	/* Muestras PCM 16-bit */
	int16_t *s = (int16_t *)buf;

	for (uint32_t i = 0; i < num_samps; i++) {
		int32_t scaled = ((int32_t)s[i] * (int32_t)gain) >> 16;

		s[i] = (int16_t)CLAMP(scaled, INT16_MIN, INT16_MAX);
	}
#else
	/* Muestras PCM 24/32-bit (en contenedor int32_t) */
	int32_t *s = (int32_t *)buf;

	for (uint32_t i = 0; i < num_samps; i++) {
		int64_t scaled = ((int64_t)s[i] * (int64_t)gain) >> 16;

		s[i] = (int32_t)CLAMP(scaled, INT32_MIN, INT32_MAX);
	}
#endif
}

/* ---------- Subscriber zbus para VOLUME_* ------------------------------ */

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
