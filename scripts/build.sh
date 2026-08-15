#!/usr/bin/env bash
# ============================================================================
# build.sh
# Compila el proyecto OBD2 Console Scanner
# ============================================================================
#
# Uso:
#   ./build.sh              # Compila con Makefile
#   ./build.sh --cmake      # Compila con CMake
#   ./build.sh --clean      # Limpia y compila
#   ./build.sh --debug      # Compila en modo debug
#   ./build.sh --run        # Compila y ejecuta
# ============================================================================

set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo -e "${BLUE}========================================================${NC}"
echo -e "${BLUE}  Compilando OBD2 Console Scanner${NC}"
echo -e "${BLUE}========================================================${NC}"

case "${1:-}" in
    --cmake)
        echo -e "${YELLOW}[*] Compilando con CMake...${NC}"
        mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)
        echo -e "${GREEN}[OK] Compilacion exitosa: build/elm327_console${NC}"
        ;;
    --debug)
        echo -e "${YELLOW}[*] Compilando en modo debug...${NC}"
        make clean 2>/dev/null || true
        CXXFLAGS="-O0 -g -DDEBUG" make -j$(nproc)
        echo -e "${GREEN}[OK] Compilacion debug exitosa: bin/elm327_console${NC}"
        ;;
    --clean)
        echo -e "${YELLOW}[*] Limpiando y compilando...${NC}"
        make clean 2>/dev/null || true
        make -j$(nproc)
        echo -e "${GREEN}[OK] Compilacion exitosa: bin/elm327_console${NC}"
        ;;
    --run)
        echo -e "${YELLOW}[*] Compilando y ejecutando...${NC}"
        make -j$(nproc)
        echo -e "${GREEN}[OK] Ejecutando...${NC}\n"
        ./bin/elm327_console
        ;;
    *)
        echo -e "${YELLOW}[*] Compilando con Makefile...${NC}"
        make -j$(nproc)
        echo -e "${GREEN}[OK] Compilacion exitosa: bin/elm327_console${NC}"
        ;;
esac
