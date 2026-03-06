#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/main.c)

target_sources_ifdef(CONFIG_CTRL_CENTRAL app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ctrl_central.c
)
