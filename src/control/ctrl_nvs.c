/*
 * ctrl_nvs.c — Persistencia de parámetros de control vía Zephyr settings/NVS.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ctrl_nvs.h"

#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ctrl_nvs, CONFIG_CTRL_NVS_LOG_LEVEL);

#define SUBTREE        "ctrl"
#define KEY_NAME       "ctrl/name"
#define KEY_VOL_DB     "ctrl/vol_db"
#define KEY_MUTED      "ctrl/muted"

#define DEFAULT_NAME   "Receptor"
#define DEFAULT_VOL_DB ((int8_t)-15)
#define DEFAULT_MUTED  false

static struct ctrl_nvs_params *g_params;

/* --- settings handlers -------------------------------------------------- */

static int ctrl_set(const char *key, size_t len,
		    settings_read_cb read_cb, void *cb_arg)
{
	if (!g_params) {
		return -EINVAL;
	}

	if (strcmp(key, "name") == 0) {
		if (len > CTRL_NVS_NAME_MAX_LEN) {
			len = CTRL_NVS_NAME_MAX_LEN;
		}
		int rc = read_cb(cb_arg, g_params->name, len);

		if (rc < 0) {
			return rc;
		}
		g_params->name[len] = '\0';
		LOG_DBG("Loaded name: %s", g_params->name);

	} else if (strcmp(key, "vol_db") == 0) {
		int rc = read_cb(cb_arg, &g_params->vol_db, sizeof(g_params->vol_db));

		if (rc < 0) {
			return rc;
		}
		LOG_DBG("Loaded vol_db: %d", g_params->vol_db);

	} else if (strcmp(key, "muted") == 0) {
		uint8_t m = 0;
		int rc = read_cb(cb_arg, &m, sizeof(m));

		if (rc < 0) {
			return rc;
		}
		g_params->muted = (m != 0);
		LOG_DBG("Loaded muted: %d", g_params->muted);
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ctrl, SUBTREE, NULL, ctrl_set, NULL, NULL);

/* --- API pública -------------------------------------------------------- */

int ctrl_nvs_init(struct ctrl_nvs_params *out)
{
	if (!out) {
		return -EINVAL;
	}

	/* Valores por defecto antes de cargar NVS */
	strncpy(out->name, DEFAULT_NAME, CTRL_NVS_NAME_MAX_LEN);
	out->name[CTRL_NVS_NAME_MAX_LEN] = '\0';
	out->vol_db = DEFAULT_VOL_DB;
	out->muted  = DEFAULT_MUTED;

	g_params = out;

	/* Cargar los valores persistidos */
	int ret = settings_load_subtree(SUBTREE);

	if (ret) {
		LOG_WRN("settings_load_subtree failed: %d (using defaults)", ret);
	}

	LOG_INF("ctrl_nvs init: name='%s' vol=%ddB muted=%d",
		out->name, out->vol_db, out->muted);
	return 0;
}

int ctrl_nvs_save_name(const char *name)
{
	if (!name) {
		return -EINVAL;
	}
	int ret = settings_save_one(KEY_NAME, name, strlen(name));

	if (ret) {
		LOG_ERR("Failed to save name: %d", ret);
	}
	return ret;
}

int ctrl_nvs_save_vol_db(int8_t vol_db)
{
	int ret = settings_save_one(KEY_VOL_DB, &vol_db, sizeof(vol_db));

	if (ret) {
		LOG_ERR("Failed to save vol_db: %d", ret);
	}
	return ret;
}

int ctrl_nvs_save_muted(bool muted)
{
	uint8_t m = muted ? 1 : 0;
	int ret = settings_save_one(KEY_MUTED, &m, sizeof(m));

	if (ret) {
		LOG_ERR("Failed to save muted: %d", ret);
	}
	return ret;
}
