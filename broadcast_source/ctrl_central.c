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

/**
 * Busca un slot por dirección BT en estado DISCONNECTED.
 * Usado para reutilizar el slot de un sink que reconecta.
 */
static struct sink_node *find_by_addr(const bt_addr_le_t *addr)
{
	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		if (sinks[i].info.state == SINK_STATE_DISCONNECTED &&
		    bt_addr_le_cmp(&sinks[i].info.addr, addr) == 0) {
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

	int ret = bt_gatt_write_without_response(n->conn, n->cmd_handle,
						 json, len, false);

	if (ret == -ENOMEM) {
		/* Buffer ACL TX transitoriamente lleno (slider en ráfaga):
		 * se descarta este write; el siguiente evento del slider
		 * enviará el valor correcto. Tratamos como éxito para que
		 * la capa superior actualice su estado optimista. */
		LOG_DBG("send_json: ACL TX buffer lleno, write descartado");
		return 0;
	}

	return ret;
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
		/* Fallo de conexión (saliente): liberar slot si estaba pre-asignado */
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
		/*
		 * Conexión ENTRANTE: el sink se conectó a nosotros (source=Peripheral).
		 * bt_mgmt.c::disconnected_cb llama bt_conn_unref(conn) siempre que
		 * CONFIG_BT_CENTRAL=y, asumiendo que toda conexión fue outgoing
		 * (y por tanto bt_conn_le_create entregó una ref extra).
		 * Para conexiones INCOMING (source=Peripheral), el stack entrega
		 * refcount=1 sin ref extra → ese unref drenaría la única ref del
		 * stack → ASSERTION FAIL [old > 0] @ conn.c:1523.
		 * Solución: tomamos una ref aquí para que bt_mgmt la consuma.
		 */
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		/* Primero buscar un slot DISCONNECTED con la misma dirección
		 * (sink que reconecta). Así preservamos nombre/vol/muted y
		 * evitamos que find_free() falle al no haber slots IDLE.
		 */
		const bt_addr_le_t *peer = bt_conn_get_dst(conn);

		n = find_by_addr(peer);
		if (n) {
			LOG_INF("sink[%d] reconectando — reutilizando slot DISCONNECTED",
				sink_index(n));
			/* Limpiar solo handles, preservar name/vol/muted */
			n->cmd_handle     = 0;
			n->evt_handle     = 0;
			n->evt_ccc_handle = 0;
		} else {
			n = find_free();
			if (!n) {
				k_mutex_unlock(&sinks_mutex);
				LOG_WRN("No hay slots libres — desconectando sink entrante");
				bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				return;
			}
			bt_addr_le_copy(&n->info.addr, peer);
		}
		bt_conn_ref(conn); /* ref extra para balancear bt_mgmt::disconnected_cb unref */
		n->conn       = conn;
		n->info.state = SINK_STATE_CONNECTING;
		k_mutex_unlock(&sinks_mutex);
	}

	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(&n->info.addr, addr_str, sizeof(addr_str));
	LOG_INF("sink[%d] connected: %s", sink_index(n), addr_str);
	start_discovery(n);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	struct sink_node *n = find_by_conn(conn);

	if (!n) {
		return;
	}

	LOG_INF("sink[%d] disconnected (reason %u)", sink_index(n), reason);

	/* NO llamar bt_conn_unref aquí: la ref extra que tomamos en connected_cb
	 * será liberada por bt_mgmt.c::disconnected_cb (tiene BT_CENTRAL=y).
	 * Doble unref aquí causaría assert [old > 0] @ conn.c. */
	n->conn          = NULL;
	n->cmd_handle    = 0;
	n->evt_handle    = 0;
	n->evt_ccc_handle = 0;
	n->info.state    = SINK_STATE_DISCONNECTED;
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level,
				    enum bt_security_err err)
{
	if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
		/*
		 * El sink tiene un bond obsoleto (p.ej. el source borró NVS).
		 * Borrar el bond en nuestro lado para que la siguiente conexión
		 * negociación sea "Just Works" sin clave previa.
		 * El sink también borrará el suyo al recibir el fallo de seguridad.
		 */
		const bt_addr_le_t *addr = bt_conn_get_dst(conn);
		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
		LOG_WRN("Borrando bond obsoleto con %s — próxima conexión sin clave",
			addr_str);
		bt_unpair(BT_ID_DEFAULT, addr);
	}
}

BT_CONN_CB_DEFINE(ctrl_central_conn_cb) = {
	.connected        = connected_cb,
	.disconnected     = disconnected_cb,
	.security_changed = security_changed_cb,
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

/*
 * scan_recv — callback registrado con bt_le_scan_cb_register.
 *
 * A diferencia de bt_le_scan_start(cb), esta API permite múltiples
 * observadores en paralelo sin conflicto con bt_mgmt (que también usa
 * el escáner cuando CONFIG_BT_CENTRAL=y).
 */
static void scan_recv(const struct bt_le_scan_recv_info *info,
		      struct net_buf_simple *buf)
{
	/* Ignorar advertising directo (ya estamos conectados o no es para nosotros) */
	if (info->adv_props & BT_GAP_ADV_PROP_DIRECTED) {
		return;
	}

	bool found = false;

	bt_data_parse(buf, adv_has_acs_uuid, &found);

	if (found) {
		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));
		LOG_INF("ACS UUID encontrado en: %s props=0x%02x type=%d",
			addr_str, info->adv_props, info->adv_type);
	}

	if (!found) {
		return;
	}

	/* ¿Ya tenemos este dispositivo? */
	k_mutex_lock(&sinks_mutex, K_FOREVER);
	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		if (sinks[i].info.state != SINK_STATE_IDLE &&
		    bt_addr_le_cmp(info->addr, &sinks[i].info.addr) == 0) {
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

	bt_addr_le_copy(&n->info.addr, info->addr);
	n->info.state = SINK_STATE_CONNECTING;
	k_mutex_unlock(&sinks_mutex);

	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));
	LOG_INF("ACS sink found: %s (rssi %d), connecting...",
		addr_str, info->rssi);

	struct bt_conn_le_create_param create_param =
		BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE,
					     BT_GAP_SCAN_FAST_INTERVAL,
					     BT_GAP_SCAN_FAST_INTERVAL);
	struct bt_le_conn_param conn_param =
		BT_LE_CONN_PARAM_INIT(BT_GAP_INIT_CONN_INT_MIN,
				      BT_GAP_INIT_CONN_INT_MAX, 0, 400);

	int ret = bt_conn_le_create(info->addr, &create_param, &conn_param,
				    &n->conn);

	if (ret) {
		LOG_ERR("bt_conn_le_create failed: %d", ret);
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		memset(n, 0, sizeof(*n));
		k_mutex_unlock(&sinks_mutex);
	}
}

static struct bt_le_scan_cb ctrl_central_scan_cb = {
	.recv = scan_recv,
};

/*
 * NOTA: El SDC v6.2 deshabilita completamente el scanner cuando el dispositivo
 * opera como BIG ISO source (confirmado: pkt=0 incluso con duty cycle del 1%).
 * El scanner se registra aquí para uso futuro cuando la arquitectura invierta
 * la dirección de conexión ACL (source como Peripheral, sinks como Centrales).
 */

/* --- API pública -------------------------------------------------------- */

int ctrl_central_init(void)
{
	memset(sinks, 0, sizeof(sinks));

	/*
	 * Registrar callback de escaneo (multi-observador, no conflicta con bt_mgmt).
	 * bt_le_scan_start NO se llama aquí: el SDC v6.2 deshabilita el scanner
	 * cuando BIG ISO está activo, lo que causaría pérdida de eventos USB NRFX
	 * al arrancar si se combina con LOG_MODE_IMMEDIATE. Las conexiones ACL se
	 * establecerán cuando la arquitectura invierta la dirección (source=Peripheral).
	 */
	bt_le_scan_cb_register(&ctrl_central_scan_cb);

	LOG_INF("ctrl_central: init OK (ACL scan deshabilitado — BIG ISO activo)");
	return 0;
}

/* --- Advertising connectable (source = Peripheral) ---------------------- */

/* Handle del ext adv set ACL — persistente entre reconexiones */
static struct bt_le_ext_adv *acl_adv_set;

static void acl_adv_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	/*
	 * Usa bt_le_ext_adv_create en lugar de bt_le_adv_start (API legacy).
	 * bt_le_adv_start asigna set 0 con PDU legacy, y el SDC rechaza mezclar
	 * legacy + extended cuando el BIG ISO ext adv ya está activo.
	 * bt_le_ext_adv_create asigna un set separado (1 o 2) con PDU extended
	 * connectable, que coexiste sin conflicto con el BIG ISO adv set.
	 */
	if (acl_adv_set) {
		/* Ya creado — solo asegurar que esté activo */
		(void)bt_le_ext_adv_start(acl_adv_set, BT_LE_EXT_ADV_START_DEFAULT);
		return;
	}

	const struct bt_le_adv_param adv_param = {
		.id           = BT_ID_DEFAULT,
		.sid          = 0U,
		.secondary_max_skip = 0U,
		/*
		 * BT_LE_ADV_OPT_CONN: connectable + scannable.
		 * BT_LE_ADV_OPT_USE_IDENTITY: dirección static random del dispositivo,
		 * la misma que el sink ve en el PA sync info del BIG.
		 * Sin BT_LE_ADV_OPT_EXT_ADV: usa PDU legacy (ADV_IND) para compatibilidad
		 * máxima con los sinks, pero a través del ext adv API (set dedicado).
		 */
		.options      = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer         = NULL,
	};

	int ret = bt_le_ext_adv_create(&adv_param, NULL, &acl_adv_set);

	if (ret) {
		LOG_ERR("acl_adv: bt_le_ext_adv_create failed: %d", ret);
		return;
	}

	static const uint8_t dev_name[] = {
		'A', 'u', 'r', 'a', 'c', 'a', 's', 't', '-', 'S', 'r', 'c'
	};
	const struct bt_data adv_data[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
		BT_DATA(BT_DATA_NAME_COMPLETE, dev_name, sizeof(dev_name)),
	};

	ret = bt_le_ext_adv_set_data(acl_adv_set, adv_data, ARRAY_SIZE(adv_data),
				     NULL, 0);
	if (ret) {
		LOG_ERR("acl_adv: bt_le_ext_adv_set_data failed: %d", ret);
		return;
	}

	ret = bt_le_ext_adv_start(acl_adv_set, BT_LE_EXT_ADV_START_DEFAULT);
	if (ret) {
		LOG_ERR("acl_adv: bt_le_ext_adv_start failed: %d", ret);
		return;
	}

	LOG_INF("ctrl_central: ACL connectable ext adv activo — sinks pueden conectar");
}

static K_WORK_DELAYABLE_DEFINE(acl_adv_work, acl_adv_work_fn);

int ctrl_central_adv_start(void)
{
	/*
	 * Programa el arranque del adv ACL 3 segundos después de ser llamado.
	 * Esto garantiza que el BIG ISO ext adv ya esté activo (arranca a ~t=1.2s)
	 * antes de que se cree el adv set ACL, evitando el conflicto SDC entre
	 * legacy advertising y extended advertising.
	 */
	k_work_schedule(&acl_adv_work, K_SECONDS(3));
	LOG_INF("ctrl_central: ACL adv programado en 3s (tras arranque BIG)");
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

int ctrl_central_get_sink_info_all(uint8_t idx, struct sink_info *out)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS || !out) {
		return -EINVAL;
	}

	k_mutex_lock(&sinks_mutex, K_FOREVER);
	if (sinks[idx].info.state == SINK_STATE_IDLE) {
		k_mutex_unlock(&sinks_mutex);
		return -ENOENT;
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
	int ret = send_json(&sinks[idx], buf);

	if (ret == 0) {
		/* Actualización optimista: el sink aplicará el volumen.
		 * Al actualizar localmente evitamos que list_sinks devuelva
		 * el valor viejo y el slider de la WebUI no se resetee. */
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		sinks[idx].info.vol_db = db;
		sinks[idx].info.muted  = false;
		k_mutex_unlock(&sinks_mutex);
	}
	return ret;
}

int ctrl_central_set_mute(uint8_t idx, bool muted)
{
	if (idx >= CTRL_CENTRAL_MAX_SINKS) {
		return -EINVAL;
	}

	char buf[64];

	snprintf(buf, sizeof(buf), "{\"cmd\":\"set_mute\",\"muted\":%s}",
		 muted ? "true" : "false");
	int ret = send_json(&sinks[idx], buf);

	if (ret == 0) {
		/* Actualización optimista: reflejar el nuevo estado mute localmente
		 * para que list_sinks devuelva el estado correcto al browser. */
		k_mutex_lock(&sinks_mutex, K_FOREVER);
		sinks[idx].info.muted = muted;
		k_mutex_unlock(&sinks_mutex);
	}
	return ret;
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
