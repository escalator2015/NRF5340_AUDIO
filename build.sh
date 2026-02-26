#!/bin/bash
# Script de build para nRF5340 Audio - Broadcast Sink
# Compatible con NCS v3.2.2

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuración del proyecto
BOARD="nrf5340dk/nrf5340/cpuapp"
EXTRA_CONF_FILE="broadcast_sink/overlay-broadcast_sink.conf"
OVERLAY_FILE="boards/nrf5340dk_nrf5340_cpuapp.overlay"

# Función de ayuda
show_help() {
    echo "Uso: $0 [opciones]"
    echo ""
    echo "Opciones:"
    echo "  -p, --pristine     Build limpio desde cero"
    echo "  -f, --flash        Flashear después de compilar"
    echo "  -d, --debug        Build con símbolos de debug"
    echo "  -r, --release      Build de release (optimizado)"
    echo "  -h, --help         Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0                 Build incremental"
    echo "  $0 -p              Build limpio"
    echo "  $0 -p -f           Build limpio y flashear"
}

# Parsear argumentos
PRISTINE=""
FLASH=""
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--pristine)
            PRISTINE="--pristine"
            shift
            ;;
        -f|--flash)
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

echo -e "${GREEN}=== nRF5340 Audio Build ===${NC}"
echo "Board: $BOARD"
echo "Config: $EXTRA_CONF_FILE"
echo "Overlay: $OVERLAY_FILE"
echo ""

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

# Build
echo -e "${YELLOW}Ejecutando build...${NC}"
west build -b "$BOARD" --sysbuild $PRISTINE -- \
    -DEXTRA_CONF_FILE="$EXTRA_CONF_FILE" \
    -DDTC_OVERLAY_FILE="$OVERLAY_FILE" \
    $EXTRA_ARGS

# Mostrar resultado
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ Build completado exitosamente${NC}"
    echo ""
    echo "Archivos generados:"
    ls -lh build/merged.hex build/*.hex 2>/dev/null || true
    
    # Flash si se solicitó
    if [ -n "$FLASH" ]; then
        echo ""
        echo -e "${YELLOW}Flasheando...${NC}"
        west flash --build-dir build
    fi
else
    echo -e "${RED}✗ Build fallido${NC}"
    exit 1
fi
