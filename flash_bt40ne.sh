#!/bin/bash
# ============================================================
# Flash Fanstel BT40NE (nRF5340) via DEBUG_OUT del nRF5340DK
# Requiere: BT40NE conectado a P19 (DEBUG_OUT) del DK
# ============================================================

set -e
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

# --- Configuración -----------------------------------------------------------
BUILD_DIR="${1:-build}"           # Directorio de build (por defecto: build)
HEX_FILE="${BUILD_DIR}/merged.hex"
HEX_CPUNET="${BUILD_DIR}/merged_CPUNET.hex"
# nrfutil-device: buscar el que tenga plugin-probe-worker funcional
find_nrfutil_device() {
    local candidates=(
        "/home/aa/.vscode/extensions/nordic-semiconductor.nrf-connect-2026.3.1484-linux-x64/platform/nrfutil/bin/nrfutil-device"
        "/home/aa/.config/nrfconnect/nrfutil-sandboxes/8.1.1/device/2.15.7/bin/nrfutil-device"
        "/home/aa/.config/nrfconnect/nrfutil-sandboxes/device/2.15.7/bin/nrfutil-device"
        "/home/aa/.config/nrfconnect/nrfutil-sandboxes/device/2.15.2/bin/nrfutil-device"
        "/home/aa/ncs/toolchains/927563c840/nrfutil/home/bin/nrfutil-device"
    )
    for candidate in "${candidates[@]}"; do
        if [ -x "$candidate" ]; then
            local libdir="$(dirname "$(dirname "$candidate")")/lib/nrfutil-device/plugin-probe-worker"
            if [ -f "$libdir" ]; then echo "$candidate"; return 0; fi
        fi
    done
    return 1
}
NRFUTIL_DEVICE=$(find_nrfutil_device 2>/dev/null) || NRFUTIL_DEVICE=""
# Device nRF5340 para JLink
JLINK_DEVICE="nRF5340_xxAA"
JLINK_SPEED="4000"
JLINK_SNR="1050010876"            # Serial Number del J-Link OB del nRF5340DK
# -----------------------------------------------------------------------------

echo -e "${GREEN}=== Flash BT40NE via DEBUG_OUT (nRF5340DK) ===${NC}"
echo "Build dir : $BUILD_DIR"
echo "App hex   : $HEX_FILE"
echo "Net hex   : $HEX_CPUNET"
echo ""

# Verificar que existe el hex
if [ ! -f "$HEX_FILE" ]; then
    echo -e "${RED}Error: No se encuentra $HEX_FILE${NC}"
    echo "Compila primero con: west build -b nrf5340dk/nrf5340/cpuapp --sysbuild"
    exit 1
fi

# ---------------------------------------------------------------
# MÉTODO 1: nrfutil-device (recomendado, auto-detecta el DK)
# ---------------------------------------------------------------
if [ -n "$NRFUTIL_DEVICE" ]; then
    echo -e "${YELLOW}[Método 1] Usando nrfutil-device ($NRFUTIL_DEVICE)...${NC}"
    echo "Dispositivos conectados:"
    "$NRFUTIL_DEVICE" list 2>/dev/null || true
    echo ""
    # --- Recover: desbloquear readback protection si está activa ---------------
    echo -e "${YELLOW}[0/2] Recover (desbloquear protección si existe)...${NC}"
    if "$NRFUTIL_DEVICE" recover \
            --serial-number "$JLINK_SNR" \
            --core Application 2>&1 | tee /tmp/recover_app.log; then
        echo -e "${GREEN}✓ Application core recuperado (o ya estaba desbloqueado)${NC}"
    else
        # Ignorar si ya estaba desbloqueado (exit code no-zero pero mensaje OK)
        grep -qi "already\|success\|done\|no protection" /tmp/recover_app.log && \
            echo -e "${GREEN}✓ Application core ya desbloqueado${NC}" || \
            echo -e "${YELLOW}AVISO: recover App core devolvió error (continúa de todas formas)${NC}"
    fi
    "$NRFUTIL_DEVICE" recover \
        --serial-number "$JLINK_SNR" \
        --core Network 2>/dev/null && \
        echo -e "${GREEN}✓ Network core recuperado${NC}" || \
        echo -e "${YELLOW}AVISO: recover Net core (ignorado)${NC}"
    echo ""

    # --- Flash Application core ------------------------------------------------
    echo -e "${YELLOW}[1/2] Flasheando Application core...${NC}"
    "$NRFUTIL_DEVICE" program \
        --firmware "$HEX_FILE" \
        --options chip_erase_mode=ERASE_ALL \
        --serial-number "$JLINK_SNR" \
        --core Application
    echo -e "${GREEN}✓ Application core flasheado${NC}"
    echo ""
    if [ -f "$HEX_CPUNET" ]; then
        echo -e "${YELLOW}[2/2] Flasheando Network core...${NC}"
        "$NRFUTIL_DEVICE" program \
            --firmware "$HEX_CPUNET" \
            --options chip_erase_mode=ERASE_ALL \
            --serial-number "$JLINK_SNR" \
            --core Network
        echo -e "${GREEN}✓ Network core flasheado${NC}"
    else
        echo -e "${YELLOW}AVISO: No se encontró $HEX_CPUNET (Network core no actualizado)${NC}"
    fi
    echo ""
    echo -e "${GREEN}=== Flash completo ===${NC}"
    exit 0
fi

# ---------------------------------------------------------------
# MÉTODO 2: JLinkExe (disponible en el sistema)
# ---------------------------------------------------------------
if command -v JLinkExe &>/dev/null; then
    echo -e "${YELLOW}[Método 2] Usando JLinkExe...${NC}"

    # Script temporal Application core
    JLINK_SCRIPT=$(mktemp /tmp/flash_bt40ne_XXXX.jlink)
    cat > "$JLINK_SCRIPT" <<JLINK
si SWD
speed ${JLINK_SPEED}
device ${JLINK_DEVICE}
connect
r
erase
loadfile ${HEX_FILE}
r
g
q
JLINK

    echo "[1/2] Flasheando Application core..."
    JLinkExe \
        -device "$JLINK_DEVICE" \
        -if SWD \
        -speed "$JLINK_SPEED" \
        -SelectEmuBySN "$JLINK_SNR" \
        -autoconnect 1 \
        -CommanderScript "$JLINK_SCRIPT"
    rm -f "$JLINK_SCRIPT"
    echo -e "${GREEN}✓ Application core flasheado${NC}"

    # Script temporal Network core (nRF5340_xxAA_NET)
    if [ -f "$HEX_CPUNET" ]; then
        JLINK_SCRIPT_NET=$(mktemp /tmp/flash_bt40ne_net_XXXX.jlink)
        cat > "$JLINK_SCRIPT_NET" <<JLINK
si SWD
speed ${JLINK_SPEED}
device nRF5340_xxAA_NET
connect
r
erase
loadfile ${HEX_CPUNET}
r
g
q
JLINK
        echo "[2/2] Flasheando Network core..."
        JLinkExe \
            -device "nRF5340_xxAA_NET" \
            -if SWD \
            -speed "$JLINK_SPEED" \
            -SelectEmuBySN "$JLINK_SNR" \
            -autoconnect 1 \
            -CommanderScript "$JLINK_SCRIPT_NET"
        rm -f "$JLINK_SCRIPT_NET"
        echo -e "${GREEN}✓ Network core flasheado${NC}"
    else
        echo -e "${YELLOW}AVISO: No se encontró $HEX_CPUNET (Network core no actualizado)${NC}"
    fi

    echo -e "${GREEN}=== Flash completado con JLinkExe ===${NC}"
    exit 0
fi

echo -e "${RED}Error: No se encontró nrfutil ni JLinkExe${NC}"
exit 1
