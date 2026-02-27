# nRF5340 Audio — BLE LE Audio Broadcast

Aplicación de audio Bluetooth Low Energy basada en el SDK nRF Connect (NCS) v3.2.2.  
Implementa **broadcast sink** (receptor) y **broadcast source** (transmisor) usando el perfil BAP BIS (Broadcast Isochronous Stream).

Compatible con dos plataformas de hardware:
- **nRF5340 DK** — placa de desarrollo estándar de Nordic Semiconductor
- **Fanstel BT40NE** — módulo nRF5340 con amplificador de potencia RF nRF21540 (FEM)

---

## Tabla de contenidos

1. [Requisitos](#requisitos)
2. [Estructura del proyecto](#estructura-del-proyecto)
3. [Targets de build](#targets-de-build)
4. [Uso del script de build](#uso-del-script-de-build)
5. [Flasheo](#flasheo)
6. [Hardware BT40NE — FEM nRF21540](#hardware-bt40ne--fem-nrf21540)
7. [Configuración de audio](#configuración-de-audio)
8. [LEDs de estado](#leds-de-estado)
9. [Parámetros relevantes de Kconfig](#parámetros-relevantes-de-kconfig)
10. [Archivos clave](#archivos-clave)

---

## Requisitos

| Herramienta | Versión mínima |
|---|---|
| nRF Connect SDK (NCS) | v3.2.2 |
| west | 1.4.0 |
| Zephyr RTOS | incluido en NCS v3.2.2 |
| nrfutil / JLinkExe | cualquier versión reciente |

Activar el entorno antes de compilar:

```bash
source ~/ncs/v3.2.2/zephyr/zephyr-env.sh
```

---

## Estructura del proyecto

```
.
├── build.sh                        # Script principal de build (ver más abajo)
├── CMakeLists.txt                  # CMake raíz (broadcast_sink + broadcast_source)
├── prj.conf                        # Kconfig base común a todos los targets
├── boards/
│   ├── nrf5340dk_nrf5340_cpuapp.overlay         # Overlay app core (I2S, gpio_fwd FEM, LEDs)
│   ├── nrf5340dk_nrf5340_cpuapp_source.overlay  # Overlay extra solo para broadcast_source (USB Audio)
│   ├── nrf5340dk_nrf5340_cpuapp.conf            # Kconfig extra board nRF5340 DK
│   └── nrf5340_audio_dk_nrf5340_cpuapp.overlay  # Overlay para nRF5340 Audio DK (referencia)
├── broadcast_sink/
│   ├── main.c
│   └── overlay-broadcast_sink.conf  # Kconfig específico del sink
├── broadcast_source/
│   ├── main.c
│   └── overlay-broadcast_source.conf # Kconfig específico del source
├── sysbuild/
│   └── ipc_radio/
│       ├── fem.conf                 # Kconfig FEM (solo BT40NE)
│       └── fem_cpunet.overlay       # DTS nRF21540 para el network core (solo BT40NE)
├── src/                            # Código fuente común (audio, BLE, LED, etc.)
└── flash_bt40ne.sh                 # Script de flasheo para BT40NE vía DEBUG_OUT del DK
```

---

## Targets de build

| Flag | Plataforma | Aplicación | Directorio de build |
|---|---|---|---|
| *(sin flag)* | nRF5340 DK | broadcast\_sink | `build/` |
| `-n` / `--bt40ne` | Fanstel BT40NE + FEM | broadcast\_sink | `build_bt40ne/` |
| `-s` / `--source` | Fanstel BT40NE + FEM | broadcast\_source | `build_bt40ne_source/` |

Todos los targets usan la placa lógica `nrf5340dk/nrf5340/cpuapp`.  
La diferencia entre DK y BT40NE es la configuración del FEM nRF21540 (activada solo para BT40NE).

---

## Uso del script de build

```bash
# Ayuda
./build.sh -h

# nRF5340 DK — broadcast_sink (build incremental)
./build.sh

# nRF5340 DK — broadcast_sink (build limpio + flash)
./build.sh -p -f

# Fanstel BT40NE — broadcast_sink (build limpio + flash vía DEBUG_OUT)
./build.sh -n -p -F

# Fanstel BT40NE — broadcast_source (build limpio + flash vía DEBUG_OUT)
./build.sh -s -p -F
```

### Opciones disponibles

| Opción | Descripción |
|---|---|
| `-n`, `--bt40ne` | Target BT40NE + FEM, broadcast_sink |
| `-s`, `--source` | Target BT40NE + FEM, broadcast_source |
| `-p`, `--pristine` | Build limpio desde cero |
| `-f`, `--flash` | Flashear tras compilar (solo nRF5340 DK, vía `west flash`) |
| `-F`, `--flash-bt40ne` | Flashear BT40NE via DEBUG_OUT del DK (`flash_bt40ne.sh`) |
| `-d`, `--debug` | Añade símbolos de debug |
| `-r`, `--release` | Optimizaciones de velocidad |

---

## Flasheo

### nRF5340 DK

```bash
west flash --build-dir build
```

O usando el flag del script:

```bash
./build.sh -p -f
```

### Fanstel BT40NE

El módulo BT40NE no tiene interfaz de debug directa. Se flashea conectándolo
al conector **DEBUG_OUT (P19)** del nRF5340 DK.

```bash
./flash_bt40ne.sh build_bt40ne          # Flashear build sink
./flash_bt40ne.sh build_bt40ne_source   # Flashear build source
```

O usando el flag `-F` del script de build:

```bash
./build.sh -n -p -F    # Compilar y flashear BT40NE sink
./build.sh -s -p -F    # Compilar y flashear BT40NE source
```

El script `flash_bt40ne.sh` intenta automáticamente dos métodos:
1. **nrfutil-device** (preferido) — detecta el DK por número de serie J-Link
2. **JLinkExe** — como alternativa si nrfutil no está disponible

---

## Hardware BT40NE — FEM nRF21540

El módulo [Fanstel BT40NE](https://www.fanstel.com/) integra un **nRF5340** (dual-core BLE + DSP)
y un **nRF21540** (Front-End Module — amplificador PA/LNA de RF).

### Modo de operación

El FEM funciona en **modo GPIO-only**: el stack MPSL controla las señales
TX_EN, RX_EN y PDN mediante GPIO en cada slot BLE.  
No se usa SPI (suficiente para BLE audio LE).

### Conexión de pines

| Señal FEM | Pin nRF5340 | Descripción |
|---|---|---|
| TX_EN | P0.21 | Activar PA (transmisión) |
| RX_EN | P0.27 | Activar LNA (recepción) |
| PDN | P0.26 | Power-down del FEM (LOW = off) |
| ANT_SEL | P1.05 | Selección de antena |
| MODE | P1.04 | Selección modo POUTA/POUTB |

### Potencia TX

| Parámetro | Valor | Descripción |
|---|---|---|
| `CONFIG_MPSL_FEM_NRF21540_TX_GAIN_DB` | 20 dB | Modo POUTA — ganancia máxima |
| `CONFIG_BT_CTLR_TX_PWR_ANTENNA` | 20 dBm | Potencia objetivo en antena |

Con estos valores, MPSL pone la radio a **0 dBm** y el PA del nRF21540 amplifica
hasta **~20 dBm** en la antena — máximo alcance.

### GPIO Forwarder (crítico)

En el nRF5340, el **network core** (donde corre MPSL/radio) no puede acceder
directamente a los GPIO del **app core**. Sin el `gpio_fwd`, MPSL cree que
controla los pines del FEM pero en realidad no hace nada → el nRF21540 queda
en power-down → potencia ~−15 dBm → alcance de cm.

El bloque `&gpio_fwd` en `boards/nrf5340dk_nrf5340_cpuapp.overlay` resuelve
esto mediante el driver IPC de reenvío de GPIO:

```dts
&gpio_fwd {
    nrf21540-gpio-if {
        gpios = <&gpio0 21 0>,  /* TX_EN  */
                <&gpio0 27 0>,  /* RX_EN  */
                <&gpio0 26 0>,  /* PDN    */
                <&gpio1 5  0>,  /* ANT_SEL*/
                <&gpio1 4  0>;  /* MODE   */
    };
};
```

---

## Configuración de audio

### broadcast_sink (receptor)

- **Fuente de salida**: I2S (DAC/codec externo)
- Recibe streams BLE BAP BIS de un transmisor broadcast
- Configura hasta 2 streams ISO simultáneos
- Latencia de presentación ajustada para fuentes externas: 40 ms

### broadcast_source (transmisor)

- **Fuente de audio**: USB Audio (siempre USB, nunca I2S)
- Transmite 2 canales estéreo codificados con LC3
- Se identifica como dispositivo USB Audio al conectarse por USB al host
- El nodo DTS `hs_0` (USB Audio headset) se aplica **únicamente** a este target
  mediante el overlay `boards/nrf5340dk_nrf5340_cpuapp_source.overlay`

---

## LEDs de estado

| LED | Pin | Estado | Descripción |
|---|---|---|---|
| `led_conn_status` | P0.28 | Parpadea | Escaneando / buscando BIG |
| `led_conn_status` | P0.28 | Fijo | Conectado / BIG sincronizado |
| `led_sync_status` | P0.29 | Fijo | LOCKED — audio sincronizado |
| `led_app_status` | P0.30 | Parpadea brevemente | Init en boot |

---

## Parámetros relevantes de Kconfig

### Buffer BLE RX (`src/audio/Kconfig`)

```kconfig
CONFIG_BUF_BLE_RX_PACKET_NUM=12   # Paquetes en el pool RX (rango 2..20, defecto 5)
CONFIG_BT_ISO_RX_BUF_COUNT=6      # Buffers ISO stack-level
```

Valores elevados (12/6) en el sink BT40NE para absorber ráfagas de paquetes
durante la re-sincronización con el BIG, reduciendo glitches de audio.

### FEM (`sysbuild/ipc_radio/fem.conf`, solo BT40NE)

```kconfig
CONFIG_MPSL_FEM=y
CONFIG_MPSL_FEM_NRF21540_GPIO=y
CONFIG_MPSL_FEM_NRF21540_TX_GAIN_DB=20
CONFIG_BT_CTLR_TX_PWR_ANTENNA=20
```

---

## Archivos clave

| Archivo | Propósito |
|---|---|
| [build.sh](build.sh) | Script de build y flash — punto de entrada principal |
| [flash_bt40ne.sh](flash_bt40ne.sh) | Flash BT40NE via DEBUG_OUT del DK |
| [prj.conf](prj.conf) | Kconfig base común |
| [boards/nrf5340dk_nrf5340_cpuapp.overlay](boards/nrf5340dk_nrf5340_cpuapp.overlay) | DTS app core: LEDs, I2S, gpio_fwd FEM |
| [boards/nrf5340dk_nrf5340_cpuapp_source.overlay](boards/nrf5340dk_nrf5340_cpuapp_source.overlay) | DTS extra source: nodo USB Audio `hs_0` |
| [sysbuild/ipc_radio/fem.conf](sysbuild/ipc_radio/fem.conf) | Kconfig nRF21540 FEM (net core) |
| [sysbuild/ipc_radio/fem_cpunet.overlay](sysbuild/ipc_radio/fem_cpunet.overlay) | DTS nRF21540 para net core |
| [broadcast_sink/overlay-broadcast_sink.conf](broadcast_sink/overlay-broadcast_sink.conf) | Kconfig específico del sink |
| [broadcast_source/overlay-broadcast_source.conf](broadcast_source/overlay-broadcast_source.conf) | Kconfig específico del source (USB Audio) |
