cd ~/nrf5340_audio

./build.sh              # Build incremental (rápido)
./build.sh -p           # Build limpio desde cero
./build.sh -p -f        # Build limpio + flashear
./build.sh -d           # Build con símbolos de debug
./build.sh -h           # Ayuda

cd ~/nrf5340_audio

# El script ya estará allí (está en el repo)
./build.sh -p -f    # Primera compilación + flash


Si en algún momento necesitas cambiar la configuración de build y actualizar el backup:

# Regenerar backup
./build.sh -p

# Actualizar archivos de config en git
cp build/nrf5340_audio_sysbuild_cache.txt build_config_export/
# ... etc

git add build_config_export/
git commit -m "Actualizar configuración de build"
git push

LRCK_M cambiado a P1.12. Configuración final libre de conflictos:

Señal I2S	Pin	nRF21540	Estado
I2S_MCK	P1.11	libre	✅
I2S_SCK_M	P1.07	libre	✅  BLANCO
I2S_LRCK_M	P1.12	libre	✅  GRIS
I2S_SDOUT	P1.09	libre	✅  MARRON
I2S_SDIN	P1.10	libre	✅

Pines I2C — TAS5828:

Señal I2C	Pin
I2C_SDA	P0.02
I2C_SCL	P0.03

# nRF5340DK (sin FEM) — broadcast_sink:
./build.sh              # incremental
./build.sh -p           # limpio
./build.sh -p -f        # limpio + flash DK

# BT40NE (con nRF21540 FEM) — broadcast_sink:
./build.sh -n           # incremental
./build.sh -n -p        # limpio
./build.sh -n -p -f     # limpio + flash BT40NE vía DEBUG_OUT

# BT40NE (con nRF21540 FEM) — broadcast_source:
./build.sh -s           # incremental
./build.sh -s -p        # limpio
./build.sh -s -p -f     # limpio + flash BT40NE vía DEBUG_OUT

# Flash manual BT40NE:
./flash_bt40ne.sh build_bt40ne          # sink
./flash_bt40ne.sh build_bt40ne_source   # source

led_sync_status	P0.29	Fijo ON	Presentation compensation LOCKED — decoder de audio en sincronía perfecta ✅
led_conn_status	P0.28	Parpadea	Escaneando el broadcaster. Se pone fijo cuando el BIG sync se establece
led_app_status	P0.30	Parpadea	Activo solo al arrancar (init de periféricos) — luego se apaga

==============================================================
Flujo en el equipo nuevo:
git clone <repo> NRF5340_AUDIO
cd NRF5340_AUDIO

# 1. Instalar NCS v3.2.2 y activar toolchain

nRF5340 DK (debugger USB integrado):
./build.sh -p        # build limpio
./build.sh -p -f     # build limpio + flash (west flash via USB)
./build.sh           # build incremental

Fanstel BT40NE — broadcast_sink (sin debugger, JTAG via DK):
./build.sh -n -p     # build limpio
./build.sh -n -p -f  # build limpio + flash (JLink via DEBUG_OUT del DK)
./build.sh -n        # build incremental

Fanstel BT40NE — broadcast_source (sin debugger, JTAG via DK):
./build.sh -s -p     # build limpio
./build.sh -s -p -f  # build limpio + flash (JLink via DEBUG_OUT del DK)
./build.sh -s        # build incremental