/*
 * Persistencia de parámetros de control del receptor en NVS (via settings).
 *
 * Claves almacenadas (subtree "ctrl"):
 *   ctrl/name    — nombre del receptor (string, max 32 chars)
 *   ctrl/vol_db  — nivel de volumen en dB (int8_t, 0 = máximo)
 *   ctrl/muted   — estado mute (uint8_t, 0/1)
 */

#ifndef _CTRL_NVS_H_
#define _CTRL_NVS_H_

#include <stdint.h>
#include <stdbool.h>

#define CTRL_NVS_NAME_MAX_LEN  32

/** Parámetros cargados desde NVS */
struct ctrl_nvs_params {
	char    name[CTRL_NVS_NAME_MAX_LEN + 1];
	int8_t  vol_db;  /* 0 = máximo, -64 = mínimo útil */
	bool    muted;
};

/**
 * Registra el manejador de settings y carga los valores desde NVS.
 * Debe llamarse después de settings_load().
 * @param out  Puntero donde se almacenan los valores leídos.
 */
int ctrl_nvs_init(struct ctrl_nvs_params *out);

/** Guarda el nombre del receptor en NVS. */
int ctrl_nvs_save_name(const char *name);

/** Guarda el nivel de volumen en NVS. */
int ctrl_nvs_save_vol_db(int8_t vol_db);

/** Guarda el estado mute en NVS. */
int ctrl_nvs_save_muted(bool muted);

#endif /* _CTRL_NVS_H_ */
