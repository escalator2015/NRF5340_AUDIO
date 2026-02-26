# Configuracion de Build nRF5340 Audio

NCS Version: v3.2.2
Board: nrf5340dk/nrf5340/cpuapp
Aplicacion: broadcast_sink

Comando de build:
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild -- -DEXTRA_CONF_FILE=broadcast_sink/overlay-broadcast_sink.conf -DDTC_OVERLAY_FILE=boards/nrf5340dk_nrf5340_cpuapp.overlay
