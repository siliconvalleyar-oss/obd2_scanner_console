#!/usr/bin/env bash
# Libera puertos rfcomm antes de conectar el ELM327
set -e

echo "[monitor] Liberando puerto Bluetooth..."

# Liberar rfcomm
rfcomm release all 2>/dev/null || true

# Verificar que no queden procesos usando rfcomm
if command -v fuser &> /dev/null; then
    fuser /dev/rfcomm* 2>/dev/null && rfcomm release all || true
fi

echo "[monitor] Puerto Bluetooth liberado"
