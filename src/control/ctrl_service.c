/*
 * ctrl_service.c — GATT Audio Control Service (ACS) para el sink.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ctrl_service.h"
#include "ctrl_nvs.h"
#include "ctrl_service_uuids.h"
#include "hw_codec.h"
#include "zbus_common.h"
#include "macros_common.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ctrl_service, CONFIG_CTRL_SERVICE_LOG_LEVEL);

/* Forward declaration del servicio GATT (definido más abajo) */
extern const struct bt_gatt_service_static acs_svc;

/* Parámetros persistidos en NVS */
static struct ctrl_nvs_params nvs_params;

/* Conexión activa del source (solo una a la vez) */
static struct bt_conn *acs_conn;

/* Buffer de respuesta JSON */
static uint8_t rsp_buf[ACS_JSON_MAX_LEN];

/* --- bt_conn callbacks — fix bt_conn_ref para bt_mgmt --------------------
 *
 * bt_mgmt.c::disconnected_cb hace bt_conn_unref(conn) cuando
 * CONFIG_BT_CENTRAL=y, asumiendo que TODA conexión fue outgoing
 * (bt_conn_le_create entregó +1 ref). Las conexiones entrantes al sink
 * (el source conecta a nosotros, nosotros somos Peripheral) no tienen
 * esa ref extra → unref lleva refcount a 0 → ASSERT [old > 0] @ conn.c.
 * Solución: tomar la ref aquí para que bt_mgmt la consuma.
 */
static void ctrl_service_connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}

	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) == 0 &&
	    info.role == BT_CONN_ROLE_PERIPHERAL) {
		bt_conn_ref(conn); /* balancea bt_mgmt::disconnected_cb unref */
		LOG_DBG("ctrl_service: ref extra tomada para conexión entrante");
	}
}

BT_CONN_CB_DEFINE(ctrl_service_conn_cb) = {
	.connected = ctrl_service_connected_cb,
};

/* --- Notificación al source -------------------------------------------- */

/*
 * Índice del atributo EVT value en la tabla acs_svc:
 * 0: Primary Service
 * 1: CMD Char declaration
 * 2: CMD value attribute
 * 3: EVT Char declaration
 * 4: EVT value attribute  ← este se usa para bt_gatt_notify()
 * 5: EVT CCC descriptor
 */
#define ACS_EVT_VALUE_ATTR_IDX 4

static void acs_notify(const char *json)
{
	if (!acs_conn) {
		return;
	}

	size_t len = strlen(json);

	if (len > ACS_JSON_MAX_LEN) {
		len = ACS_JSON_MAX_LEN;
	}

	int ret = bt_gatt_notify(acs_conn, &acs_svc.attrs[ACS_EVT_VALUE_ATTR_IDX],
				 json, len);

	if (ret && ret != -ENOTCONN) {
		LOG_WRN("ACS notify failed: %d", ret);
	}
}

/* --- Helpers de volumen ------------------------------------------------- */

/*
 * Convierte dB (0..-64) al valor de hw_codec (0..128):
 * hw_val = (db + 64) * 2
 */
static uint8_t db_to_hw(int8_t db)
{
	int16_t v = ((int16_t)db + 64) * 2;

	if (v < 0) {
		v = 0;
	}
	if (v > 128) {
		v = 128;
	}
	return (uint8_t)v;
}

/* --- Publicar en zbus volumes channel ---------------------------------- */

ZBUS_CHAN_DECLARE(volume_chan);

static void publish_volume_set(uint8_t hw_val)
{
	struct volume_msg msg = {
		.event  = VOLUME_SET,
		.volume = hw_val,
	};
	int ret = zbus_chan_pub(&volume_chan, &msg, K_MSEC(100));

	if (ret) {
		LOG_WRN("Failed to publish volume: %d", ret);
	}
}

static void publish_mute(bool muted)
{
	struct volume_msg msg = {
		.event  = muted ? VOLUME_MUTE : VOLUME_UNMUTE,
		.volume = 0,
	};
	int ret = zbus_chan_pub(&volume_chan, &msg, K_MSEC(100));

	if (ret) {
		LOG_WRN("Failed to publish mute: %d", ret);
	}
}

/* --- Tabla de dispatch de comandos JSON -------------------------------- */

typedef int (*cmd_handler_fn)(const char *payload, char *rsp, size_t rsp_max);

struct ctrl_cmd_handler {
	const char     *cmd;
	cmd_handler_fn  fn;
};

static int cmd_get_info(const char *payload, char *rsp, size_t rsp_max)
{
	ARG_UNUSED(payload);

	bt_addr_le_t addr;
	size_t count = 1;
	char addr_str[13]; /* 6 bytes hex sin separadores */

	bt_id_get(&addr, &count);
	snprintf(addr_str, sizeof(addr_str),
		 "%02X%02X%02X%02X%02X%02X",
		 addr.a.val[5], addr.a.val[4], addr.a.val[3],
		 addr.a.val[2], addr.a.val[1], addr.a.val[0]);

	snprintf(rsp, rsp_max,
		 "{\"id\":\"%s\",\"name\":\"%s\",\"vol_db\":%d,\"muted\":%s}",
		 addr_str,
		 nvs_params.name,
		 nvs_params.vol_db,
		 nvs_params.muted ? "true" : "false");
	return 0;
}

static int cmd_set_vol(const char *payload, char *rsp, size_t rsp_max)
{
	/* Buscar campo "db" en el JSON */
	const char *p = strstr(payload, "\"db\":");

	if (!p) {
		snprintf(rsp, rsp_max, "{\"ok\":false,\"err\":\"missing db\"}");
		return -EINVAL;
	}
	p += 5;

	long db = 0;
	char *end;

	db = strtol(p, &end, 10);
	if (p == end) {
		snprintf(rsp, rsp_max, "{\"ok\":false,\"err\":\"invalid db\"}");
		return -EINVAL;
	}

	/* Clamp -64..0 */
	if (db > 0) {
		db = 0;
	} else if (db < -64) {
		db = -64;
	}

	nvs_params.vol_db = (int8_t)db;
	nvs_params.muted  = false;

	publish_volume_set(db_to_hw(nvs_params.vol_db));
	ctrl_nvs_save_vol_db(nvs_params.vol_db);
	ctrl_nvs_save_muted(nvs_params.muted);

	snprintf(rsp, rsp_max, "{\"ok\":true}");
	LOG_INF("Volume set to %d dB", nvs_params.vol_db);
	return 0;
}

static int cmd_set_mute(const char *payload, char *rsp, size_t rsp_max)
{
	bool muted;

	if (strstr(payload, "\"muted\":true")) {
		muted = true;
	} else if (strstr(payload, "\"muted\":false")) {
		muted = false;
	} else {
		snprintf(rsp, rsp_max, "{\"ok\":false,\"err\":\"missing muted\"}");
		return -EINVAL;
	}

	nvs_params.muted = muted;
	publish_mute(muted);
	ctrl_nvs_save_muted(muted);

	snprintf(rsp, rsp_max, "{\"ok\":true}");
	LOG_INF("Mute set to %d", muted);
	return 0;
}

static int cmd_set_name(const char *payload, char *rsp, size_t rsp_max)
{
	const char *p = strstr(payload, "\"name\":\"");

	if (!p) {
		snprintf(rsp, rsp_max, "{\"ok\":false,\"err\":\"missing name\"}");
		return -EINVAL;
	}
	p += 8; /* saltar "name":" */

	const char *end = strchr(p, '"');

	if (!end) {
		snprintf(rsp, rsp_max, "{\"ok\":false,\"err\":\"unterminated name\"}");
		return -EINVAL;
	}

	size_t len = (size_t)(end - p);

	if (len > CTRL_NVS_NAME_MAX_LEN) {
		len = CTRL_NVS_NAME_MAX_LEN;
	}

	memcpy(nvs_params.name, p, len);
	nvs_params.name[len] = '\0';

	ctrl_nvs_save_name(nvs_params.name);

	snprintf(rsp, rsp_max, "{\"ok\":true}");
	LOG_INF("Name set to '%s'", nvs_params.name);
	return 0;
}

/* Tabla extensible de comandos — añadir nuevos handlers aquí */
static const struct ctrl_cmd_handler cmd_table[] = {
	{ "get_info", cmd_get_info },
	{ "set_vol",  cmd_set_vol  },
	{ "set_mute", cmd_set_mute },
	{ "set_name", cmd_set_name },
	{ NULL, NULL }   /* centinela */
};

static void dispatch_cmd(const char *json, size_t len)
{
	/* Copiar a buffer local terminado en '\0' */
	char buf[ACS_JSON_MAX_LEN + 1];
	size_t copy_len = MIN(len, ACS_JSON_MAX_LEN);

	memcpy(buf, json, copy_len);
	buf[copy_len] = '\0';

	LOG_DBG("ACS CMD: %s", buf);

	/* Extraer campo "cmd" */
	const char *p = strstr(buf, "\"cmd\":\"");

	if (!p) {
		LOG_WRN("ACS: no 'cmd' field");
		acs_notify("{\"ok\":false,\"err\":\"no cmd\"}");
		return;
	}
	p += 7;
	const char *cmd_end = strchr(p, '"');

	if (!cmd_end) {
		acs_notify("{\"ok\":false,\"err\":\"bad cmd\"}");
		return;
	}

	char cmd_name[32];
	size_t cmd_len = MIN((size_t)(cmd_end - p), sizeof(cmd_name) - 1);

	memcpy(cmd_name, p, cmd_len);
	cmd_name[cmd_len] = '\0';

	/* Buscar en la tabla */
	for (int i = 0; cmd_table[i].cmd != NULL; i++) {
		if (strcmp(cmd_table[i].cmd, cmd_name) == 0) {
			memset(rsp_buf, 0, sizeof(rsp_buf));
			cmd_table[i].fn(buf, (char *)rsp_buf, sizeof(rsp_buf));
			acs_notify((char *)rsp_buf);
			return;
		}
	}

	LOG_WRN("ACS: unknown cmd '%s'", cmd_name);
	snprintf((char *)rsp_buf, sizeof(rsp_buf),
		 "{\"ok\":false,\"err\":\"unknown cmd\"}");
	acs_notify((char *)rsp_buf);
}

/* --- Callbacks GATT ----------------------------------------------------- */

static ssize_t acs_cmd_write(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);

	acs_conn = conn;
	dispatch_cmd((const char *)buf, len);
	return len;
}

static void acs_evt_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_DBG("ACS EVT CCC changed: %d", value);
}

/* --- Definición del servicio GATT -------------------------------------- */

BT_GATT_SERVICE_DEFINE(acs_svc,
	BT_GATT_PRIMARY_SERVICE(ACS_SERVICE_UUID),

	/* CMD: Write Without Response */
	BT_GATT_CHARACTERISTIC(ACS_CMD_CHAR_UUID,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE,
			       NULL, acs_cmd_write, NULL),

	/* EVT: Notify */
	BT_GATT_CHARACTERISTIC(ACS_EVT_CHAR_UUID,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(acs_evt_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* --- API pública -------------------------------------------------------- */

int ctrl_service_uuid_populate(struct net_buf_simple *uuid_buf)
{
	static const uint8_t acs_uuid[] = { ACS_SERVICE_UUID_VAL };

	if (net_buf_simple_tailroom(uuid_buf) < sizeof(acs_uuid)) {
		LOG_ERR("No room for ACS UUID in adv buf");
		return -ENOMEM;
	}

	net_buf_simple_add_mem(uuid_buf, acs_uuid, sizeof(acs_uuid));
	return 0;
}

int ctrl_service_init(void)
{
	int ret;

	ret = ctrl_nvs_init(&nvs_params);
	if (ret) {
		LOG_ERR("ctrl_nvs_init failed: %d", ret);
		return ret;
	}

	/* Aplicar volumen y mute cargados de NVS */
	publish_volume_set(db_to_hw(nvs_params.vol_db));
	if (nvs_params.muted) {
		publish_mute(true);
	}

	LOG_INF("Audio Control Service registered");
	LOG_INF("  name='%s'  vol=%ddB  muted=%d",
		nvs_params.name, nvs_params.vol_db, nvs_params.muted);
	return 0;
}
