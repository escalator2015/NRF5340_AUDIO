/*
 * usb_serial_ctrl.h — Canal de control USB CDC-ACM sobre JSON+newline.
 *
 * Expone los mismos comandos que el canal BLE GATT (ctrl_central) a través
 * de un puerto serie virtual USB, de modo que una página web (WebSerial API)
 * puede controlar los receptores Auracast conectados al source.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _USB_SERIAL_CTRL_H_
#define _USB_SERIAL_CTRL_H_

/**
 * Inicializa el módulo USB CDC-ACM de control.
 *
 * Abre el dispositivo CDC-ACM, arranca el hilo de lectura y queda
 * a la espera de comandos JSON terminados en '\n' desde el host USB.
 *
 * Debe llamarse después de ctrl_central_init().
 *
 * @return 0 si ok, código de error negativo en caso de fallo.
 */
int usb_serial_ctrl_init(void);

#endif /* _USB_SERIAL_CTRL_H_ */
