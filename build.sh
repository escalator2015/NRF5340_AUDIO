#!/bin/bash
# Script de build para nRF5340 Audio
# Compatible con NCS v3.2.2
#
# ┌─────────────────────────────────────────────────────────────────────────┐
# │  TARGET           │ FLAG │ BUILD DIR           │ FLASH                  │
# ├─────────────────────────────────────────────────────────────────────────┤
# │ nRF5340 DK        │ (def)│ build/              │ -f  → west flash (USB) │
# │   broadcast_sink  │      │                     │   debugger integrado   │
# ├─────────────────────────────────────────────────────────────────────────┤
# │ Fanstel BT40NE    │  -n  │ build_bt40ne/       │ -F  → flash_bt40ne.sh  │
# │   broadcast_sink  │      │                     │   via DEBUG_OUT del DK │
# │   (sin debugger)  │      │                     │   (cable JTAG al DK)   │
# ├─────────────────────────────────────────────────────────────────────────┤
# │ Fanstel BT40NE    │  -s  │ build_bt40ne_source/│ -F  → flash_bt40ne.sh  │
# │   broadcast_source│      │                     │   via DEBUG_OUT del DK │
# │   (sin debugger)  │      │                     │   (cable JTAG al DK)   │
# └─────────────────────────────────────────────────────────────────────────┘
#
# NOTA: Las placas BT40NE no tienen debugger propio. Para flashearlas hay
#       que conectarlas al conector DEBUG_OUT (P19) del nRF5340DK mediante
#       el cable JTAG, y usar -F (flash_bt40ne.sh / JLink).

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Serial number del J-Link OB del nRF5340DK
# (evita ambigüedad cuando el DEBUG_OUT también tiene un target conectado)
JLINK_DK_SNR="1050010876"

# Configuración común
BOARD="nrf5340dk/nrf5340/cpuapp"
EXTRA_CONF_FILE="broadcast_sink/overlay-broadcast_sink.conf"
OVERLAY_FILE="boards/nrf5340dk_nrf5340_cpuapp.overlay"
SOURCE_MODE=""

# Función de ayuda
show_help() {
    echo "Uso: $0 [opciones]"
    echo ""
    echo "Targets (placas):" 
    echo "  (defecto)          nRF5340 DK — broadcast_sink       (tiene debugger USB propio)"
    echo "  -n, --bt40ne       Fanstel BT40NE — broadcast_sink   (sin debugger, JTAG via DK)"
    echo "  -s, --source       Fanstel BT40NE — broadcast_source (sin debugger, JTAG via DK)"
    echo ""
    echo "Opciones de build:"
    echo "  -p, --pristine     Build limpio desde cero"
    echo "  -d, --debug        Build con símbolos de debug"
    echo "  -r, --release      Build de release (optimizado)"
    echo "  -h, --help         Mostrar esta ayuda"
    echo ""
    echo "Opciones de flash:"
    echo "  -f, --flash        Flashea el target actual (funciona para TODOS los targets):"
    echo "                       · nRF5340DK  → west flash via USB (debugger integrado)"
    echo "                       · BT40NE     → flash_bt40ne.sh via DEBUG_OUT del DK (JLink)"
    echo "  -F                 Alias de -f (compatibilidad)"
    echo ""
    echo "Ejemplos:"
    echo "  $0                 Build incremental nRF5340DK (broadcast_sink)"
    echo "  $0 -p              Build limpio nRF5340DK"
    echo "  $0 -p -f           Build limpio + flash nRF5340DK (USB)"
    echo "  $0 -n -p           Build limpio BT40NE broadcast_sink"
    echo "  $0 -n -p -f        Build limpio + flash BT40NE sink (via DEBUG_OUT del DK)"
    echo "  $0 -s -p           Build limpio BT40NE broadcast_source"
    echo "  $0 -s -p -f        Build limpio + flash BT40NE source (via DEBUG_OUT del DK)"
}

# Parsear argumentos
PRISTINE=""
FLASH=""        # flashea el target actual (DK o BT40NE)
BT40NE=""
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--bt40ne)
            BT40NE="1"
            shift
            ;;
        -s|--source)
            BT40NE="1"
            SOURCE_MODE="1"
            shift
            ;;
        -p|--pristine)
            PRISTINE="--pristine"
            shift
            ;;
        -f|--flash|-F|--flash-bt40ne)
            FLASH="1"
            shift
            ;;
        -d|--debug)
            EXTRA_ARGS="${EXTRA_ARGS} -DCONFIG_DEBUG_OPTIMIZATIONS=y"
            shift
            ;;
        -r|--release)
            EXTRA_ARGS="${EXTRA_ARGS} -DCONFIG_SPEED_OPTIMIZATIONS=y"
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Opción desconocida: $1"
            show_help
            exit 1
            ;;
    esac
done

# Verificar que estamos en el directorio correcto
if [ ! -f "prj.conf" ]; then
    echo -e "${RED}Error: No se encuentra prj.conf${NC}"
    echo "Ejecuta este script desde el directorio raíz del proyecto"
    exit 1
fi

# Verificar west
if ! command -v west &> /dev/null; then
    echo -e "${RED}Error: west no encontrado${NC}"
    echo "Activa el entorno de NCS: source ~/ncs/v3.2.2/zephyr/zephyr-env.sh"
    exit 1
fi

# --- Configurar target ---------------------------------------------------------
PROJ_DIR="$(pwd)"
if [ -n "$BT40NE" ]; then
    # Fanstel BT40NE: no tiene debugger propio.
    # Se flashea conectándola al conector DEBUG_OUT (P19) del nRF5340DK via cable JTAG.
    HAS_DEBUGGER=""
    FEM_ARGS="-Dipc_radio_EXTRA_CONF_FILE=${PROJ_DIR}/sysbuild/ipc_radio/fem.conf \
              -Dipc_radio_EXTRA_DTC_OVERLAY_FILE=${PROJ_DIR}/sysbuild/ipc_radio/fem_cpunet.overlay"
    if [ -n "$SOURCE_MODE" ]; then
        BUILD_DIR="build_bt40ne_source"
        TARGET_NAME="Fanstel BT40NE — broadcast_source  [sin debugger | flash via DEBUG_OUT del DK]"
        EXTRA_CONF_FILE="broadcast_source/overlay-broadcast_source.conf"
        OVERLAY_FILE="boards/nrf5340dk_nrf5340_cpuapp.overlay;boards/nrf5340dk_nrf5340_cpuapp_source.overlay"
    else
        BUILD_DIR="build_bt40ne"
        TARGET_NAME="Fanstel BT40NE — broadcast_sink    [sin debugger | flash via DEBUG_OUT del DK]"
        OVERLAY_FILE="boards/nrf5340dk_nrf5340_cpuapp.overlay;boards/nrf5340dk_nrf5340_cpuapp_sink.overlay"
    fi
else
    # nRF5340 DK: tiene debugger USB integrado (J-Link OB).
    # Se flashea directamente con west flash via USB.
    HAS_DEBUGGER="1"
    BUILD_DIR="build"
    TARGET_NAME="nRF5340 DK — broadcast_sink           [debugger USB integrado | flash via west]"
    FEM_ARGS=""
    OVERLAY_FILE="boards/nrf5340dk_nrf5340_cpuapp.overlay;boards/nrf5340dk_nrf5340_cpuapp_sink.overlay"
fi


echo -e "${GREEN}=== nRF5340 Audio Build ===${NC}"
echo -e "Target : ${BLUE}${TARGET_NAME}${NC}"
echo "Board  : $BOARD"
echo "Config : $EXTRA_CONF_FILE"
echo "Overlay: $OVERLAY_FILE"
echo "Dir    : $BUILD_DIR"
echo ""

# Build
echo -e "${YELLOW}Ejecutando build...${NC}"
west build -b "$BOARD" --sysbuild $PRISTINE \
    --build-dir "$BUILD_DIR" -- \
    -DEXTRA_CONF_FILE="$EXTRA_CONF_FILE" \
    -DDTC_OVERLAY_FILE="$OVERLAY_FILE" \
    $FEM_ARGS \
    $EXTRA_ARGS

echo ""
echo -e "${GREEN}✓ Build completado exitosamente${NC}"
echo ""
echo "Archivos generados:"
ls -lh "${BUILD_DIR}/merged.hex" "${BUILD_DIR}"/*.hex 2>/dev/null || true

# Flash — método automático según target
if [ -n "$FLASH" ]; then
    if [ -n "$HAS_DEBUGGER" ]; then
        echo ""
        echo -e "${YELLOW}┌─────────────────────────────────────────────────────────────┐${NC}"
        echo -e "${YELLOW}│  IMPORTANTE: desconecta el cable JTAG del conector P19 (DEBUG_OUT)  │${NC}"
        echo -e "${YELLOW}│  del nRF5340DK antes de continuar.                          │${NC}"
        echo -e "${YELLOW}│                                                              │${NC}"
        echo -e "${YELLOW}│  Mientras la BT40NE esté conectada al P19, el J-Link OB     │${NC}"
        echo -e "${YELLOW}│  del DK enruta hacia ella y flashea la BT40NE en lugar      │${NC}"
        echo -e "${YELLOW}│  del nRF5340 integrado en el DK.                            │${NC}"
        echo -e "${YELLOW}└─────────────────────────────────────────────────────────────┘${NC}"
        echo ""
        read -rp "Pulsa ENTER cuando el cable P19 esté desconectado..."
        echo ""
        echo -e "${YELLOW}Flasheando nRF5340DK via USB (debugger integrado, SNR=$JLINK_DK_SNR)...${NC}"
        west flash --build-dir "$BUILD_DIR" --dev-id "$JLINK_DK_SNR"
    else
        echo ""
        echo -e "${YELLOW}Flasheando BT40NE via DEBUG_OUT del DK (JLink)...${NC}"
        echo -e "${YELLOW}(Asegúrate de que la BT40NE está conectada al conector P19 del DK)${NC}"
        ./flash_bt40ne.sh "$BUILD_DIR"
    fi
fi
