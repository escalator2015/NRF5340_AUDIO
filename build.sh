#!/bin/bash
# Script de build para nRF5340 Audio - Broadcast Sink
# Compatible con NCS v3.2.2
#
# Targets:
#   (defecto)  nRF5340 DK            → build dir: build/
#   --bt40ne   Fanstel BT40NE + FEM  → build dir: build_bt40ne/

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuración común
BOARD="nrf5340dk/nrf5340/cpuapp"
EXTRA_CONF_FILE="broadcast_sink/overlay-broadcast_sink.conf"
OVERLAY_FILE="boards/nrf5340dk_nrf5340_cpuapp.overlay"

# Función de ayuda
show_help() {
    echo "Uso: $0 [opciones]"
    echo ""
    echo "Opciones:"
    echo "  -n, --bt40ne       Build para Fanstel BT40NE (nRF21540 FEM)"
    echo "  -p, --pristine     Build limpio desde cero"
    echo "  -f, --flash        Flashear después de compilar (solo nRF5340DK)"
    echo "  -F, --flash-bt40ne Flashear BT40NE vía DEBUG_OUT después de compilar"
    echo "  -d, --debug        Build con símbolos de debug"
    echo "  -r, --release      Build de release (optimizado)"
    echo "  -h, --help         Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0                 Build incremental para nRF5340DK"
    echo "  $0 -p              Build limpio para nRF5340DK"
    echo "  $0 -p -f           Build limpio + flash nRF5340DK"
    echo "  $0 -n              Build incremental para BT40NE"
    echo "  $0 -n -p           Build limpio para BT40NE"
    echo "  $0 -n -p -F        Build limpio + flash BT40NE"
}

# Parsear argumentos
PRISTINE=""
FLASH=""
FLASH_BT40NE=""
BT40NE=""
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--bt40ne)
            BT40NE="1"
            shift
            ;;
        -p|--pristine)
            PRISTINE="--pristine"
            shift
            ;;
        -f|--flash)
            FLASH="1"
            shift
            ;;
        -F|--flash-bt40ne)
            FLASH_BT40NE="1"
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
if [ -n "$BT40NE" ]; then
    BUILD_DIR="build_bt40ne"
    TARGET_NAME="Fanstel BT40NE (nRF5340 + nRF21540 FEM)"
    PROJ_DIR="$(pwd)"
    FEM_ARGS="-Dipc_radio_EXTRA_CONF_FILE=${PROJ_DIR}/sysbuild/ipc_radio/fem.conf \
              -Dipc_radio_EXTRA_DTC_OVERLAY_FILE=${PROJ_DIR}/sysbuild/ipc_radio/fem_cpunet.overlay"
else
    BUILD_DIR="build"
    TARGET_NAME="nRF5340 DK"
    FEM_ARGS=""
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

# Flash
if [ -n "$FLASH" ]; then
    echo ""
    echo -e "${YELLOW}Flasheando nRF5340DK...${NC}"
    west flash --build-dir "$BUILD_DIR"
fi

if [ -n "$FLASH_BT40NE" ]; then
    echo ""
    echo -e "${YELLOW}Flasheando BT40NE via DEBUG_OUT...${NC}"
    ./flash_bt40ne.sh "$BUILD_DIR"
fi
