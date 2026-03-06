/*
 * Audio Control Service (ACS) — GATT service en el sink.
 *
 * Expone dos características:
 *   ACS_CMD_CHAR  (Write Without Response): recibe comandos JSON del source.
 *   ACS_EVT_CHAR  (Notify): envía respuestas/eventos JSON al source.
 *
 * Protocolo JSON (ejemplos):
 *   Comando:   {"cmd":"get_info"}
 *   Respuesta: {"id":"C7B9EA91A368","name":"Sala","vol_db":-15,"muted":false}
 *
 *   Comando:   {"cmd":"set_vol","db":-20}
 *   Respuesta: {"ok":true}
 *
 *   Comando:   {"cmd":"set_mute","muted":true}
 *   Respuesta: {"ok":true}
 *
 *   Comando:   {"cmd":"set_name","name":"Sala"}
 *   Respuesta: {"ok":true}
 */

#ifndef _CTRL_SERVICE_H_
#define _CTRL_SERVICE_H_

#include <zephyr/bluetooth/conn.h>

/**
 * Inicializa el Audio Control Service.
 * Debe llamarse antes de bt_mgmt_adv_start().
 * Registra el servicio GATT y carga parámetros desde NVS.
 */
int ctrl_service_init(void);

/**
 * Añade el UUID del ACS al buffer de advertising.
 * Llamar desde ext_adv_populate().
 */
int ctrl_service_uuid_populate(struct net_buf_simple *uuid_buf);

#endif /* _CTRL_SERVICE_H_ */
