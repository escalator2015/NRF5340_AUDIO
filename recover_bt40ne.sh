#!/bin/bash
# Recover nRF5340 en Fanstel BT40NE - Desactiva APPROTECT y borra flash
# Requiere BT40NE conectada a DEBUG_OUT (P19) del nRF5340DK (S/N: 1050010876)

set -e
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

SNR="1050010876"
FAMILY="nrf53"

# Buscar nrfutil-device con plugin-probe-worker funcional
find_nrfutil_device() {
    # Prioridad: extensión VS Code > sandboxes > toolchain
    local candidates=(
        "/home/aa/.vscode/extensions/nordic-semiconductor.nrf-connect-2026.1.1327-linux-x64/platform/nrfutil/bin/nrfutil-device"
        "/home/aa/.config/nrfconnect/nrfutil-sandboxes/8.1.1/device/2.15.7/bin/nrfutil-device"
        "/home/aa/.config/nrfconnect/nrfutil-sandboxes/device/2.15.7/bin/nrfutil-device"
        "/home/aa/.config/nrfconnect/nrfutil-sandboxes/device/2.15.2/bin/nrfutil-device"
        "/home/aa/ncs/toolchains/927563c840/nrfutil/home/bin/nrfutil-device"
    )
    for candidate in "${candidates[@]}"; do
        if [ -x "$candidate" ]; then
            # Verificar que tiene su worker en lib/
            local libdir="$(dirname "$(dirname "$candidate")")/lib/nrfutil-device/plugin-probe-worker"
            if [ -f "$libdir" ]; then
                echo "$candidate"
                return 0
            fi
        fi
    done
    return 1
}

NRFUTIL_DEVICE=$(find_nrfutil_device) || {
    echo -e "${RED}Error: No se encontró nrfutil-device con plugin-probe-worker${NC}"
    exit 1
}

echo -e "${GREEN}=== Recover BT40NE (nRF5340, S/N: $SNR) ===${NC}"
echo "nrfutil-device: $NRFUTIL_DEVICE"
echo ""
echo -e "${YELLOW}ADVERTENCIA: Esto borrará toda la flash de la BT40NE${NC}"
echo -n "¿Continuar? [s/N]: "
read -r resp
[[ "$resp" =~ ^[sS]$ ]] || { echo "Cancelado."; exit 0; }
echo ""

echo -e "${YELLOW}[1/2] Recuperando Application core...${NC}"
"$NRFUTIL_DEVICE" recover --serial-number "$SNR" --core Application --family "$FAMILY"
echo -e "${GREEN}✓ Application core recuperado${NC}"

echo ""
echo -e "${YELLOW}[2/2] Recuperando Network core...${NC}"
"$NRFUTIL_DEVICE" recover --serial-number "$SNR" --core Network --family "$FAMILY"
echo -e "${GREEN}✓ Network core recuperado${NC}"

echo ""
echo -e "${GREEN}=== Recover completo. La BT40NE ya puede ser flasheada. ===${NC}"
echo ""
echo "Ahora ejecuta:"
echo "  ./flash_bt40ne.sh build_sink    # para sink"
echo "  ./flash_bt40ne.sh build_source  # para source"
