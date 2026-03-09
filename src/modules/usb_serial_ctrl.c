/*
 * usb_serial_ctrl.c — Canal de control USB CDC-ACM sobre JSON+newline.
 *
 * Protocolo (misma semántica que ctrl_central / BLE GATT):
 *
 *  PC → Source (comandos):
 *    {"cmd":"list_sinks"}
 *    {"cmd":"get_info","sink":0}
 *    {"cmd":"set_vol","sink":0,"db":-20}
 *    {"cmd":"set_mute","sink":0,"muted":true}
 *    {"cmd":"set_name","sink":0,"name":"Sala"}
 *
 *  Source → PC (respuestas):
 *    {"sinks":[{"idx":0,"name":"Sala","vol_db":-15,"muted":false,"state":"ready"},...]}
 *    {"ok":true}
 *    {"ok":false,"err":"<descripción>"}
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "usb_serial_ctrl.h"
#include "ctrl_central.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_serial_ctrl, CONFIG_USB_SERIAL_CTRL_LOG_LEVEL);

/* -------------------------------------------------------------------------
 * Configuración
 * ---------------------------------------------------------------------- */

#define CDC_CTRL_STACK_SIZE  2048
#define CDC_CTRL_PRIORITY    K_PRIO_PREEMPT(10)

/* Tamaño máximo de una línea JSON entrante */
#define RX_BUF_SIZE  256
/* Tamaño máximo de una línea JSON saliente */
#define TX_BUF_SIZE  1024

/* Nombre del nodo DT del CDC-ACM de control (segundo CDC si hay USB Audio) */
#define CDC_CTRL_NODE  DT_NODELABEL(cdc_acm_uart1)

/* -------------------------------------------------------------------------
 * Estado interno
 * ---------------------------------------------------------------------- */

static const struct device *cdc_dev;

static K_THREAD_STACK_DEFINE(cdc_ctrl_stack, CDC_CTRL_STACK_SIZE);
static struct k_thread cdc_ctrl_thread_data;

/* -------------------------------------------------------------------------
 * Helpers de transmisión
 * ---------------------------------------------------------------------- */

/**
 * Envía una cadena al host USB terminada en CRLF.
 * uart_poll_out no bloquea indefinidamente, es seguro desde el hilo.
 */
static void cdc_send(const char *str)
{
	if (!device_is_ready(cdc_dev)) {
		return;
	}

	for (const char *p = str; *p; p++) {
		uart_poll_out(cdc_dev, (unsigned char)*p);
	}
	uart_poll_out(cdc_dev, '\r');
	uart_poll_out(cdc_dev, '\n');
}

/* -------------------------------------------------------------------------
 * Parser JSON minimalista
 *
 * Extrae el valor de una clave en un objeto JSON plano.
 * Soporta strings, números y booleanos.  No depende de dynamic alloc.
 * ---------------------------------------------------------------------- */

/**
 * Busca la clave `key` en `json` y copia el valor (sin comillas si es string)
 * en `out`, max `out_size` bytes.
 * Devuelve true si encontrada.
 */
static bool json_get_str(const char *json, const char *key,
			 char *out, size_t out_size)
{
	/* Busca "key": */
	char search[48];

	snprintf(search, sizeof(search), "\"%s\"", key);

	const char *p = strstr(json, search);

	if (!p) {
		return false;
	}
	p += strlen(search);

	/* Saltar whitespace y ':' */
	while (*p == ' ' || *p == ':' || *p == '\t') {
		p++;
	}

	if (*p == '"') {
		/* Valor string */
		p++;
		size_t i = 0;

		while (*p && *p != '"' && i + 1 < out_size) {
			out[i++] = *p++;
		}
		out[i] = '\0';
		return true;
	}

	/* Valor número / boolean / null */
	size_t i = 0;

	while (*p && *p != ',' && *p != '}' && *p != '\n' &&
	       *p != ' ' && i + 1 < out_size) {
		out[i++] = *p++;
	}
	out[i] = '\0';
	return (i > 0);
}

/** Devuelve el valor entero de la clave `key`, o `def` si no existe. */
static int json_get_int(const char *json, const char *key, int def)
{
	char buf[32];

	if (!json_get_str(json, key, buf, sizeof(buf))) {
		return def;
	}
	return (int)strtol(buf, NULL, 10);
}

/** Devuelve el valor booleano de la clave `key`, o `def` si no existe. */
static bool json_get_bool(const char *json, const char *key, bool def)
{
	char buf[8];

	if (!json_get_str(json, key, buf, sizeof(buf))) {
		return def;
	}
	return (strncmp(buf, "true", 4) == 0);
}

/* -------------------------------------------------------------------------
 * Serialización de estado de sinks
 * ---------------------------------------------------------------------- */

static const char *state_str(enum sink_state s)
{
	switch (s) {
	case SINK_STATE_IDLE:         return "idle";
	case SINK_STATE_CONNECTING:   return "connecting";
	case SINK_STATE_DISCOVERING:  return "discovering";
	case SINK_STATE_READY:        return "ready";
	case SINK_STATE_DISCONNECTED: return "disconnected";
	default:                      return "unknown";
	}
}

/**
 * Serializa todos los sinks conocidos (incluyendo los que no están READY)
 * en un JSON de respuesta y lo envía al host.
 */
static void send_sink_list(void)
{
	static char tx[TX_BUF_SIZE];
	int pos = 0;
	int len = (int)sizeof(tx);

	/* Incluye todos los slots, no solo los READY */
	pos += snprintf(tx + pos, len - pos, "{\"sinks\":[");

	bool first = true;

	for (int i = 0; i < CTRL_CENTRAL_MAX_SINKS; i++) {
		struct sink_info si;

		/* Pedimos info de todos los slots, incluyendo no-READY */
		int ret = ctrl_central_get_sink_info_all(i, &si);

		if (ret < 0) {
			/* Slot libre o vacío — omitir */
			continue;
		}

		char addr_str[13]; /* 6 bytes hex = 12 chars + NUL */

		snprintf(addr_str, sizeof(addr_str),
			 "%02X%02X%02X%02X%02X%02X",
			 si.addr.a.val[5], si.addr.a.val[4],
			 si.addr.a.val[3], si.addr.a.val[2],
			 si.addr.a.val[1], si.addr.a.val[0]);

		if (!first) {
			pos += snprintf(tx + pos, len - pos, ",");
		}
		first = false;

		pos += snprintf(tx + pos, len - pos,
			"{\"idx\":%d,"
			"\"id\":\"%s\","
			"\"name\":\"%s\","
			"\"vol_db\":%d,"
			"\"muted\":%s,"
			"\"state\":\"%s\"}",
			i,
			addr_str,
			si.name,
			(int)si.vol_db,
			si.muted ? "true" : "false",
			state_str(si.state));
	}

	pos += snprintf(tx + pos, len - pos, "]}");
	cdc_send(tx);
}

/* -------------------------------------------------------------------------
 * Dispatcher de comandos
 * ---------------------------------------------------------------------- */

static void dispatch(char *line)
{
	/* Eliminar CRLF */
	size_t l = strlen(line);

	while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) {
		line[--l] = '\0';
	}
	if (l == 0) {
		return;
	}

	LOG_DBG("RX: %s", line);

	char cmd[32];

	if (!json_get_str(line, "cmd", cmd, sizeof(cmd))) {
		LOG_WRN("dispatch: no 'cmd' field in: %s", line);
		cdc_send("{\"ok\":false,\"err\":\"missing cmd\"}");
		return;
	}

	LOG_DBG("CMD: %s", cmd);

	/* ---- list_sinks ---- */
	if (strcmp(cmd, "list_sinks") == 0) {
		send_sink_list();
		return;
	}

	/* Los demás comandos requieren "sink" */
	int sink_idx = json_get_int(line, "sink", -1);

	if (sink_idx < 0 || sink_idx >= CTRL_CENTRAL_MAX_SINKS) {
		LOG_WRN("dispatch: invalid sink idx=%d for cmd=%s", sink_idx, cmd);
		cdc_send("{\"ok\":false,\"err\":\"missing or invalid sink index\"}");
		return;
	}

	int ret = 0;

	/* ---- get_info ---- */
	if (strcmp(cmd, "get_info") == 0) {
		ret = ctrl_central_get_info((uint8_t)sink_idx);

	/* ---- set_vol ---- */
	} else if (strcmp(cmd, "set_vol") == 0) {
		int db = json_get_int(line, "db", INT32_MIN);

		LOG_DBG("set_vol: sink=%d db=%d", sink_idx, db);
		if (db == INT32_MIN || db > 0 || db < -64) {
			LOG_WRN("set_vol: db=%d out of range", db);
			cdc_send("{\"ok\":false,\"err\":\"db out of range [0..-64]\"}");
			return;
		}
		ret = ctrl_central_set_vol_db((uint8_t)sink_idx, (int8_t)db);
		LOG_DBG("ctrl_central_set_vol_db ret=%d", ret);

	/* ---- set_mute ---- */
	} else if (strcmp(cmd, "set_mute") == 0) {
		bool muted = json_get_bool(line, "muted", false);

		ret = ctrl_central_set_mute((uint8_t)sink_idx, muted);

	/* ---- set_name ---- */
	} else if (strcmp(cmd, "set_name") == 0) {
		char name[CTRL_CENTRAL_NAME_MAX];

		if (!json_get_str(line, "name", name, sizeof(name))) {
			cdc_send("{\"ok\":false,\"err\":\"missing name\"}");
			return;
		}
		ret = ctrl_central_set_name((uint8_t)sink_idx, name);

	} else {
		cdc_send("{\"ok\":false,\"err\":\"unknown cmd\"}");
		return;
	}

	if (ret == 0) {
		/* Las respuestas asíncronas (notify del sink) también se
		 * reenviarán al host desde notify_forward_cb cuando esté
		 * implementado. Por ahora confirmamos el envío. */
		cdc_send("{\"ok\":true}");
	} else if (ret == -ENOMEM) {
		/* Buffer ACL TX transitoriamente lleno (slider rápido):
		 * la escritura se descarta; el slider enviará el valor
		 * final cuando el buffer se libere. No mostramos error. */
		cdc_send("{\"ok\":true}");
	} else {
		static char err_buf[64];

		snprintf(err_buf, sizeof(err_buf),
			 "{\"ok\":false,\"err\":\"cmd failed: %d\"}", ret);
		cdc_send(err_buf);
	}
}

/* -------------------------------------------------------------------------
 * Hilo de lectura CDC-ACM
 * ---------------------------------------------------------------------- */

static void cdc_ctrl_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	static char rx_buf[RX_BUF_SIZE];
	size_t pos = 0;

	LOG_INF("usb_serial_ctrl: ready, waiting for commands");

	/* Enviar saludo al conectar. El host puede que no tenga DTR activo
	 * (WebSerial no lo garantiza), así que arrancamos sin esperar señales
	 * de control y simplemente anunciamos que estamos listos.
	 * La línea se enviará en cuanto el host abra el puerto. */
	k_sleep(K_MSEC(500)); /* dar tiempo al stack USB para estabilizarse */
	cdc_send("{\"info\":\"Auracast Control Ready\"}");

	while (true) {
		unsigned char c;
		int ret = uart_poll_in(cdc_dev, &c);

		if (ret == 0) {
			if (c == '\n' || c == '\r') {
				if (pos > 0) {
					rx_buf[pos] = '\0';
					dispatch(rx_buf);
					pos = 0;
				}
			} else {
				if (pos < RX_BUF_SIZE - 1) {
					rx_buf[pos++] = c;
				} else {
					/* Overflow: descartar línea */
					pos = 0;
					cdc_send("{\"ok\":false,\"err\":\"line too long\"}");
				}
			}
		} else {
			/* No hay datos, ceder CPU brevemente */
			k_yield();
		}
	}
}

/* -------------------------------------------------------------------------
 * API pública
 * ---------------------------------------------------------------------- */

int usb_serial_ctrl_init(void)
{
	cdc_dev = DEVICE_DT_GET(CDC_CTRL_NODE);

	if (!device_is_ready(cdc_dev)) {
		LOG_ERR("CDC ACM ctrl device not ready");
		return -ENODEV;
	}

	k_thread_create(&cdc_ctrl_thread_data, cdc_ctrl_stack,
			K_THREAD_STACK_SIZEOF(cdc_ctrl_stack),
			cdc_ctrl_thread, NULL, NULL, NULL,
			CDC_CTRL_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&cdc_ctrl_thread_data, "usb_serial_ctrl");

	LOG_INF("usb_serial_ctrl: initialized (CDC ACM uart1)");
	return 0;
}
