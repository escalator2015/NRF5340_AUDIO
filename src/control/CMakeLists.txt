#
# Copyright (c) 2026
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Exponer src/control/ como directorio de includes para toda la app
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources_ifdef(CONFIG_CTRL_NVS app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ctrl_nvs.c
)

target_sources_ifdef(CONFIG_CTRL_SERVICE app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ctrl_service.c
)
