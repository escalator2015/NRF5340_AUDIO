#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/main.c)

target_sources_ifdef(CONFIG_CTRL_CENTRAL app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ctrl_central.c
)

target_sources_ifdef(CONFIG_USB_SERIAL_CTRL app PRIVATE
  ${CMAKE_SOURCE_DIR}/src/modules/usb_serial_ctrl.c
)

# ctrl_central.h está en broadcast_source/, añadirlo al include path
# para que usb_serial_ctrl.c pueda encontrarlo
if(CONFIG_USB_SERIAL_CTRL)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
