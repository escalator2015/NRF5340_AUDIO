/*
 * Driver para TAS5828 (Texas Instruments) — I2C, control de volumen y mute.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "tas5828.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tas5828, CONFIG_TAS5828_LOG_LEVEL);

/* Nodo I2C del TAS5828 definido en el overlay del board:
 *   &i2c1 { tas5828: tas5828@2a { compatible = "ti,tas5828"; reg = <0x2a>; }; };
 */
#define TAS5828_NODE DT_NODELABEL(tas5828)

static const struct i2c_dt_spec tas5828_dev =
	I2C_DT_SPEC_GET(TAS5828_NODE);

static uint8_t current_ctrl2; /* shadow del registro DEVICE_CTRL_2 */

static int tas5828_reg_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = {reg, val};
	int ret = i2c_write_dt(&tas5828_dev, buf, sizeof(buf));

	if (ret) {
		LOG_ERR("I2C write reg 0x%02x failed: %d", reg, ret);
	}
	return ret;
}

static int tas5828_reg_read(uint8_t reg, uint8_t *val)
{
	int ret = i2c_write_read_dt(&tas5828_dev, &reg, 1, val, 1);

	if (ret) {
		LOG_ERR("I2C read reg 0x%02x failed: %d", reg, ret);
	}
	return ret;
}

int tas5828_init(void)
{
	if (!device_is_ready(tas5828_dev.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* Leer estado inicial de DEVICE_CTRL_2 */
	int ret = tas5828_reg_read(TAS5828_REG_DEVICE_CTRL2, &current_ctrl2);

	if (ret) {
		return ret;
	}

	LOG_INF("TAS5828 init OK (CTRL2=0x%02x)", current_ctrl2);
	return 0;
}

int tas5828_volume_set_db(int8_t vol_db)
{
	/* Limitar rango: 0dB máximo, -127dB mínimo */
	if (vol_db > 0) {
		vol_db = 0;
	} else if (vol_db < -127) {
		vol_db = -127;
	}

	uint8_t reg_val = TAS5828_DB_TO_REG(vol_db);
	int ret = tas5828_reg_write(TAS5828_REG_DIG_VOL_CTL, reg_val);

	if (!ret) {
		LOG_DBG("Volume set to %d dB (reg=0x%02x)", vol_db, reg_val);
	}
	return ret;
}

int tas5828_mute(bool mute)
{
	if (mute) {
		current_ctrl2 |= TAS5828_MUTE_BIT;
	} else {
		current_ctrl2 &= ~TAS5828_MUTE_BIT;
	}

	int ret = tas5828_reg_write(TAS5828_REG_DEVICE_CTRL2, current_ctrl2);

	if (!ret) {
		LOG_DBG("Mute %s", mute ? "ON" : "OFF");
	}
	return ret;
}
