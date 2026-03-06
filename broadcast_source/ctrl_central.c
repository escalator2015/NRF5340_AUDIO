/*
 * ctrl_central.c — BT Central para control de sinks Auracast.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ctrl_central.h"
#include "ctrl_service_uuids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ctrl_central, CONFIG_CTRL_CENTRAL_LOG_LEVEL);

/* UUID persistente para GATT discover de CCC (0x2902) — el puntero debe  */
/* mantenerse válido durante toda la operación asíncrona de discover.       */
static const struct bt_uuid_16 ccc_uuid_g = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

/* --- Tabla interna de sinks -------------------------------------------- */

struct sink_node {
	struct sink_info   info;
	struct bt_conn    *conn;
	uint16_t           cmd_handle;   /* Handle de CMD char (write) */
	uint16_t           evt_handle;   /* Handle de EVT char (notify) */
	uint16_t           evt_ccc_handle; /* Handle CCC de EVT */
	struct bt_gatt_discover_params disc_params;
	struct bt_gatt_subscribe_params sub_params;
	/* Buffers de descubrimiento */
	struct bt_uuid_128 svc_uuid;
	struct bt_uuid_128 cmd_uuid;
	struct bt_uuid_128 evt_uuid;
};

static struct sink_node sinks[CTRL_CENTRAL_MAX_SINKS];
static K_MUTEX_DEFINE(sinks_mutex);

/* --- Helpers -------------------------------------------------------------- */

static struct sink_node *find_by_conn(struct bt_conn *conn)
{
	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		if (sinks[i].conn == conn) {
			return &sinks[i];
		}
	}
	return NULL;
}

static struct sink_node *find_free(void)
{
	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		if (sinks[i].info.state == SINK_STATE_IDLE) {
			return &sinks[i];
		}
	}
	return NULL;
}

static int sink_index(const struct sink_node *n)
{
	return (int)(n - sinks);
}

/* --- Envío de comando JSON ---------------------------------------------- */

static int send_json(struct sink_node *n, const char *json)
{
	if (!n->conn || n->info.state != SINK_STATE_READY || !n->cmd_handle) {
		return -ENODEV;
	}

	size_t len = strlen(json);

	if (len > ACS_JSON_MAX_LEN) {
		len = ACS_JSON_MAX_LEN;
	}

	return bt_gatt_write_without_response(n->conn, n->cmd_handle,
					      json, len, false);
}

/* --- Callback de notificación EVT -------------------------------------- */

static uint8_t notify_cb(struct bt_conn *conn,
			 struct bt_gatt_subscribe_params *params,
			 const void *data, uint16_t length)
{
	if (!data) {
		LOG_DBG("ACS notify unsubscribed");
		return BT_GATT_ITER_STOP;
	}

	char buf[ACS_JSON_MAX_LEN + 1];
	size_t len = MIN(length, ACS_JSON_MAX_LEN);

	memcpy(buf, data, len);
	buf[len] = '\0';

	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return BT_GATT_ITER_CONTINUE;
	}

	int idx = sink_index(n);

	LOG_INF("sink[%d] notify: %s", idx, buf);

	/* Parsear respuesta de get_info para actualizar tabla local */
	const char *name_p = strstr(buf, "\"name\":\"");

	if (name_p) {
		name_p += 8;
		const char *end = strchr(name_p, '"');

		if (end) {
			size_t nlen = MIN((size_t)(end - name_p),
					  CTRL_CENTRAL_NAME_MAX - 1);
			k_mutex_lock(&sinks_mutex, K_FOREVER);
			memcpy(n->info.name, name_p, nlen);
			n->info.name[nlen] = '\0';
			k_mutex_unlock(&sinks_mutex);
		}
	}

	const char *vol_p = strstr(buf, "\"vol_db\":");

	if (vol_p) {
		vol_p += 9;
		long v = strtol(vol_p, NULL, 10);
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		n->info.vol_db = (int8_t)v;
		k_mutex_unlock(&sinks_mutex);
	}

	if (strstr(buf, "\"muted\":true")) {
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		n->info.muted = true;
		k_mutex_unlock(&sinks_mutex);
	} else if (strstr(buf, "\"muted\":false")) {
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		n->info.muted = false;
		k_mutex_unlock(&sinks_mutex);
	}

	return BT_GATT_ITER_CONTINUE;
}

/* --- GATT discover ------------------------------------------------------- */

static uint8_t discover_ccc_cb(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return BT_GATT_ITER_STOP;
	}

	if (!attr) {
		/* Discovery completo */
		if (!n->evt_ccc_handle) {
			LOG_WRN("sink[%d] EVT CCC not found", sink_index(n));
			n->info.state = SINK_STATE_READY; /* continuar de todos modos */
			return BT_GATT_ITER_STOP;
		}

		/* Suscribir notificaciones */
		n->sub_params.notify          = notify_cb;
		n->sub_params.value           = BT_GATT_CCC_NOTIFY;
		n->sub_params.ccc_handle      = n->evt_ccc_handle;
		n->sub_params.value_handle    = n->evt_handle;

		int ret = bt_gatt_subscribe(conn, &n->sub_params);

		if (ret && ret != -EALREADY) {
			LOG_ERR("sink[%d] subscribe failed: %d", sink_index(n), ret);
		} else {
			LOG_INF("sink[%d] subscribed to EVT notify", sink_index(n));
		}

		n->info.state = SINK_STATE_READY;
		LOG_INF("sink[%d] READY", sink_index(n));

		/* Solicitar info inicial */
		ctrl_central_get_info((uint8_t)sink_index(n));
		return BT_GATT_ITER_STOP;
	}

	/* Buscar CCC (UUID 0x2902) */
	if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
		n->evt_ccc_handle = attr->handle;
		LOG_DBG("sink[%d] CCC handle=0x%04x", sink_index(n), n->evt_ccc_handle);
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_chars_cb(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  struct bt_gatt_discover_params *params)
{
	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return BT_GATT_ITER_STOP;
	}

	if (!attr) {
		/* Fin de discover de chars: buscar CCC de EVT */
		if (!n->evt_handle) {
			LOG_WRN("sink[%d] EVT char not found", sink_index(n));
			n->info.state = SINK_STATE_READY;
			return BT_GATT_ITER_STOP;
		}

		n->disc_params.uuid     = &ccc_uuid_g.uuid;
		n->disc_params.func     = discover_ccc_cb;
		n->disc_params.start_handle = n->evt_handle + 1;
		n->disc_params.end_handle   = 0xffff;
		n->disc_params.type     = BT_GATT_DISCOVER_DESCRIPTOR;

		int ret = bt_gatt_discover(conn, &n->disc_params);

		if (ret) {
			LOG_ERR("sink[%d] CCC discover failed: %d", sink_index(n), ret);
			n->info.state = SINK_STATE_READY;
		}
		return BT_GATT_ITER_STOP;
	}

	struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

	if (!bt_uuid_cmp(chrc->uuid, ACS_CMD_CHAR_UUID)) {
		n->cmd_handle = chrc->value_handle;
		LOG_DBG("sink[%d] CMD handle=0x%04x", sink_index(n), n->cmd_handle);
	} else if (!bt_uuid_cmp(chrc->uuid, ACS_EVT_CHAR_UUID)) {
		n->evt_handle = chrc->value_handle;
		LOG_DBG("sink[%d] EVT handle=0x%04x", sink_index(n), n->evt_handle);
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_svc_cb(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return BT_GATT_ITER_STOP;
	}

	if (!attr) {
		LOG_WRN("sink[%d] ACS service not found", sink_index(n));
		n->info.state = SINK_STATE_DISCONNECTED;
		return BT_GATT_ITER_STOP;
	}

	struct bt_gatt_service_val *svc = attr->user_data;

	LOG_DBG("sink[%d] ACS service found end=0x%04x", sink_index(n), svc->end_handle);

	/* Descubrir características dentro del servicio */
	memcpy(&n->cmd_uuid, ACS_CMD_CHAR_UUID, sizeof(struct bt_uuid_128));
	n->disc_params.uuid         = ACS_CMD_CHAR_UUID;
	n->disc_params.func         = discover_chars_cb;
	n->disc_params.start_handle = attr->handle + 1;
	n->disc_params.end_handle   = svc->end_handle;
	n->disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

	/* Descubrir todas las características del servicio ACS sin filtro UUID */
	n->disc_params.uuid         = NULL;

	int ret = bt_gatt_discover(conn, &n->disc_params);

	if (ret) {
		LOG_ERR("sink[%d] char discover failed: %d", sink_index(n), ret);
		n->info.state = SINK_STATE_DISCONNECTED;
	}

	return BT_GATT_ITER_STOP;
}

static void start_discovery(struct sink_node *n)
{
	n->info.state = SINK_STATE_DISCOVERING;

	memcpy(&n->svc_uuid, ACS_SERVICE_UUID, sizeof(struct bt_uuid_128));
	n->disc_params.uuid         = &n->svc_uuid.uuid;
	n->disc_params.func         = discover_svc_cb;
	n->disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	n->disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	n->disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

	int ret = bt_gatt_discover(n->conn, &n->disc_params);

	if (ret) {
		LOG_ERR("sink[%d] service discover start failed: %d",
			sink_index(n), ret);
		n->info.state = SINK_STATE_DISCONNECTED;
	}
}

/* --- Callbacks de conexión ACL ----------------------------------------- */

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		/* Fallo de conexión: liberar slot */
		struct sink_node *n = find_by_conn(conn);

		if (n) {
			LOG_WRN("sink[%d] connect failed: %u",
				sink_index(n), err);
			bt_conn_unref(n->conn);
			memset(n, 0, sizeof(*n));
		}
		return;
	}

	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return;
	}

	LOG_INF("sink[%d] connected", sink_index(n));
	start_discovery(n);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return;
	}

	LOG_INF("sink[%d] disconnected (reason %u)", sink_index(n), reason);

	bt_conn_unref(n->conn);
	n->conn          = NULL;
	n->cmd_handle    = 0;
	n->evt_handle    = 0;
	n->evt_ccc_handle = 0;
	n->info.state    = SINK_STATE_DISCONNECTED;
}

BT_CONN_CB_DEFINE(ctrl_central_conn_cb) = {
	.connected    = connected_cb,
	.disconnected = disconnected_cb,
};

/* --- Scanning ------------------------------------------------------------ */

static bool adv_has_acs_uuid(struct bt_data *data, void *user_data)
{
	bool *found = user_data;

	if (data->type != BT_DATA_UUID128_ALL &&
	    data->type != BT_DATA_UUID128_SOME) {
		return true; /* continuar iterando */
	}

	static const uint8_t acs_uuid_bytes[] = { ACS_SERVICE_UUID_VAL };

	/* Iterar UUIDs de 16 bytes en el campo */
	for (int i = 0; i + 16 <= data->data_len; i += 16) {
		if (memcmp(&data->data[i], acs_uuid_bytes, 16) == 0) {
			*found = true;
			return false; /* parar iteración */
		}
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
		    uint8_t adv_type, struct net_buf_simple *buf)
{
	bool found = false;

	bt_data_parse(buf, adv_has_acs_uuid, &found);

	if (!found) {
		return;
	}

	/* ¿Ya tenemos este dispositivo? */
	k_mutex_lock(&sinks_mutex, K_FOREVER);
	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		if (sinks[i].info.state != SINK_STATE_IDLE &&
		    bt_addr_le_cmp(addr, &sinks[i].info.addr) == 0) {
			k_mutex_unlock(&sinks_mutex);
			return; /* ya conocido */
		}
	}

	struct sink_node *n = find_free();

	if (!n) {
		k_mutex_unlock(&sinks_mutex);
		LOG_WRN("No free sink slots");
		return;
	}

	bt_addr_le_copy(&n->info.addr, addr);
	n->info.state = SINK_STATE_CONNECTING;
	k_mutex_unlock(&sinks_mutex);

	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("ACS sink found: %s (rssi %d), connecting...", addr_str, rssi);

	struct bt_conn_le_create_param create_param =
		BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE,
					     BT_GAP_SCAN_FAST_INTERVAL,
					     BT_GAP_SCAN_FAST_INTERVAL);
	struct bt_le_conn_param conn_param =
		BT_LE_CONN_PARAM_INIT(BT_GAP_INIT_CONN_INT_MIN,
				      BT_GAP_INIT_CONN_INT_MAX, 0, 400);

	int ret = bt_conn_le_create(addr, &create_param, &conn_param, &n->conn);

	if (ret) {
		LOG_ERR("bt_conn_le_create failed: %d", ret);
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		memset(n, 0, sizeof(*n));
		k_mutex_unlock(&sinks_mutex);
	}
}

static struct bt_le_scan_param scan_params = {
	.type     = BT_LE_SCAN_TYPE_ACTIVE,
	.options  = BT_LE_SCAN_OPT_NONE,
	.interval = BT_GAP_SCAN_FAST_INTERVAL,
	.window   = BT_GAP_SCAN_FAST_WINDOW,
};

/* --- API pública -------------------------------------------------------- */

int ctrl_central_init(void)
{
	memset(sinks, 0, sizeof(sinks));

	int ret = bt_le_scan_start(&scan_params, scan_cb);

	if (ret && ret != -EALREADY) {
		LOG_ERR("Scan start failed: %d", ret);
		return ret;
	}

	LOG_INF("ctrl_central: scanning for ACS sinks...");
	return 0;
}

int ctrl_central_sink_count(void)
{
	int count = 0;

	k_mutex_lock(&sinks_mutex, K_FOREVER);
	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		if (sinks[i].info.state == SINK_STATE_READY) {
			count++;
		}
	}
	k_mutex_unlock(&sinks_mutex);
	return count;
}

int ctrl_central_get_sink_info(uint8_t idx, struct sink_info *out)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS || !out) {
		return -EINVAL;
	}

	k_mutex_lock(&sinks_mutex, K_FOREVER);
	if (sinks[idx].info.state != SINK_STATE_READY) {
		k_mutex_unlock(&sinks_mutex);
		return -ENODEV;
	}
	memcpy(out, &sinks[idx].info, sizeof(*out));
	k_mutex_unlock(&sinks_mutex);
	return 0;
}

int ctrl_central_get_info(uint8_t idx)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS) {
		return -EINVAL;
	}
	return send_json(&sinks[idx], "{\"cmd\":\"get_info\"}");
}

int ctrl_central_set_vol_db(uint8_t idx, int8_t db)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS) {
		return -EINVAL;
	}

	char buf[64];

	snprintf(buf, sizeof(buf), "{\"cmd\":\"set_vol\",\"db\":%d}", (int)db);
	return send_json(&sinks[idx], buf);
}

int ctrl_central_set_mute(uint8_t idx, bool muted)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS) {
		return -EINVAL;
	}

	char buf[64];

	snprintf(buf, sizeof(buf), "{\"cmd\":\"set_mute\",\"muted\":%s}",
		 muted ? "true" : "false");
	return send_json(&sinks[idx], buf);
}

int ctrl_central_set_name(uint8_t idx, const char *name)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS || !name) {
		return -EINVAL;
	}

	char buf[ACS_JSON_MAX_LEN];

	snprintf(buf, sizeof(buf), "{\"cmd\":\"set_name\",\"name\":\"%s\"}", name);
	return send_json(&sinks[idx], buf);
}
