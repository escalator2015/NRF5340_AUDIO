/*
 * ctrl_central.c/h — BT Central que gestiona los sinks Auracast.
 *
 * El source escanea advertising que contenga el UUID del ACS,
 * conecta a cada sink, realiza GATT discover y suscribe notificaciones.
 * Expone una API para obtener info y enviar comandos a cada sink.
 */

#ifndef _CTRL_CENTRAL_H_
#define _CTRL_CENTRAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/bluetooth/conn.h>

#define CTRL_CENTRAL_MAX_SINKS  4
#define CTRL_CENTRAL_NAME_MAX   33

/** Estado de cada sink conocido */
enum sink_state {
	SINK_STATE_IDLE = 0,       /* Slot libre */
	SINK_STATE_CONNECTING,     /* Conexión ACL en curso */
	SINK_STATE_DISCOVERING,    /* GATT discover en curso */
	SINK_STATE_READY,          /* Listo para enviar comandos */
	SINK_STATE_DISCONNECTED,   /* Desconectado, pendiente de reconexión */
};

/** Información de un sink */
struct sink_info {
	bt_addr_le_t       addr;
	enum sink_state    state;
	char               name[CTRL_CENTRAL_NAME_MAX];
	int8_t             vol_db;
	bool               muted;
};

/**
 * Inicializa el módulo.
 * Activa scanning BLE buscando dispositivos con UUID ACS.
 * Llamar tras bt_mgmt_init().
 */
int ctrl_central_init(void);

/** Devuelve el número de sinks actualmente en estado READY. */
int ctrl_central_sink_count(void);

/**
 * Obtiene información de un sink por índice (0-based).
 * @param idx  Índice del sink (0 .. sink_count-1)
 * @param out  Puntero donde copiar la info
 * @return 0 si ok, -ENODEV si idx inválido o sink no en estado READY
 */
int ctrl_central_get_sink_info(uint8_t idx, struct sink_info *out);

/**
 * Solicita al sink[idx] su información actual (get_info).
 * La respuesta llegará como log. En fases futuras se publicará vía callback.
 */
int ctrl_central_get_info(uint8_t idx);

/**
 * Establece el volumen del sink[idx].
 * @param idx   Índice del sink
 * @param db    Nivel en dB: 0 (máximo) .. -64 (mínimo)
 */
int ctrl_central_set_vol_db(uint8_t idx, int8_t db);

/**
 * Activa o desactiva el mute del sink[idx].
 */
int ctrl_central_set_mute(uint8_t idx, bool muted);

/**
 * Cambia el nombre del sink[idx] (se guarda en el NVS del sink).
 * @param name  Nuevo nombre (max 32 chars)
 */
int ctrl_central_set_name(uint8_t idx, const char *name);

#endif /* _CTRL_CENTRAL_H_ */
