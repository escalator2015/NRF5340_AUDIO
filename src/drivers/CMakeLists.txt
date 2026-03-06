#
# Copyright (c) 2022 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources_ifdef(CONFIG_NRF5340_AUDIO_CS47L63_DRIVER app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/cs47l63_comm.c
)

target_sources_ifdef(CONFIG_TAS5828_DRIVER app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tas5828.c
)
