/*
 * Driver para amplificador TAS5828 de Texas Instruments (I2C).
 *
 * Registros principales:
 *   0x03  DEVICE_CTRL_2  — bit 3: MUTE (1=muted)
 *   0x4C  DIG_VOL_CTL    — volumen digital: 0x00=0dBFS, paso -0.5dB/LSB
 *
 * Dirección I2C: 0x2A (ADDR pin a GND); configurable por overlay.
 */

#ifndef _TAS5828_H_
#define _TAS5828_H_

#include <stdint.h>
#include <stdbool.h>

/* Registros */
#define TAS5828_REG_DEVICE_CTRL2   0x03
#define TAS5828_REG_DIG_VOL_CTL    0x4C

/* Bits de DEVICE_CTRL_2 */
#define TAS5828_MUTE_BIT           BIT(3)

/* Conversión dB → registro DIG_VOL_CTL
 * vol_db: 0 (máximo) .. -127 (mínimo), paso 1dB
 * reg = |vol_db| * 2  (cada LSB = 0.5dB)
 * Límite hardware: 0x00=0dB, 0xFE=-127dB
 */
#define TAS5828_DB_TO_REG(db)  ((uint8_t)((-((int8_t)(db))) * 2))

/** Inicializa el driver (debe llamarse una vez, tras I2C listo). */
int tas5828_init(void);

/**
 * Establece el volumen digital.
 * @param vol_db  Nivel en dB: 0 (máximo) .. -64 (mínimo útil)
 */
int tas5828_volume_set_db(int8_t vol_db);

/** Activa o desactiva el mute hardware. */
int tas5828_mute(bool mute);

#endif /* _TAS5828_H_ */
