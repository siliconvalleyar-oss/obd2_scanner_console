#!/usr/bin/env bash
# ============================================================================
# clear_rfcomm.sh
# Libera puertos rfcomm y prepara Bluetooth para conexion ELM327
# ============================================================================
#
# Uso:
#   chmod +x clear_rfcomm.sh
#   ./clear_rfcomm.sh
# ============================================================================

set -euo pipefail

echo "[monitor] Liberando puerto Bluetooth..."

rfcomm release all 2>/dev/null || true

if command -v fuser &>/dev/null; then
    fuser /dev/rfcomm* 2>/dev/null && rfcomm release all || true
fi

if ls /dev/rfcomm* 2>/dev/null; then
    echo "[monitor] Puertos rfcomm aun presentes, intentando liberacion forzada..."
    for dev in /dev/rfcomm*; do
        rfcomm release "$(basename "$dev")" 2>/dev/null || true
    done
fi

if ! lsmod | grep -q rfcomm; then
    echo "[monitor] Cargando modulo rfcomm..."
    sudo modprobe rfcomm 2>/dev/null || echo "[monitor] No se pudo cargar rfcomm (no critico si ya esta)"
fi

echo "[monitor] Puerto Bluetooth listo para conexion"
