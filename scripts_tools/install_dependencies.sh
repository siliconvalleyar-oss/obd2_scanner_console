#!/usr/bin/env bash
# ============================================================================
# install_dependencies.sh
# Instala todas las dependencias necesarias para compilar y ejecutar
# OBD2 Console Scanner (ELM327 + Bluetooth)
# ============================================================================
#
# Uso:
#   chmod +x install_dependencies.sh
#   ./install_dependencies.sh
#
# Soporta: Ubuntu, Debian, Fedora, RHEL, Arch Linux
# ============================================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================================${NC}"
echo -e "${BLUE}  Instalando dependencias OBD2 Console Scanner${NC}"
echo -e "${BLUE}========================================================${NC}"

if [[ $EUID -eq 0 ]]; then
    SUDO=""
else
    SUDO="sudo"
fi

# Detectar distribución
detect_distro() {
    if command -v apt &>/dev/null; then
        echo "debian"
    elif command -v dnf &>/dev/null; then
        echo "fedora"
    elif command -v yum &>/dev/null; then
        echo "rhel"
    elif command -v pacman &>/dev/null; then
        echo "arch"
    else
        echo "unknown"
    fi
}

DISTRO=$(detect_distro)
echo -e "${GREEN}[OK] Distribucion detectada: ${DISTRO}${NC}"

install_debian() {
    echo -e "${YELLOW}[*] Actualizando lista de paquetes...${NC}"
    $SUDO apt update

    echo -e "${YELLOW}[*] Instalando dependencias requeridas...${NC}"
    $SUDO apt install -y \
        build-essential \
        cmake \
        libbluetooth-dev \
        pkg-config

    echo -e "${YELLOW}[*] Instalando herramientas opcionales...${NC}"
    $SUDO apt install -y \
        bluetooth \
        bluez \
        bluez-tools \
        rfkill \
        net-tools \
        doxygen \
        graphviz || true
}

install_fedora() {
    echo -e "${YELLOW}[*] Instalando dependencias requeridas...${NC}"
    $SUDO dnf install -y \
        gcc-c++ \
        cmake \
        bluez-libs-devel \
        pkgconfig

    echo -e "${YELLOW}[*] Instalando herramientas opcionales...${NC}"
    $SUDO dnf install -y \
        bluez \
        bluez-tools \
        rfkill \
        doxygen \
        graphviz || true
}

install_rhel() {
    echo -e "${YELLOW}[*] Instalando dependencias requeridas...${NC}"
    $SUDO yum install -y \
        gcc-c++ \
        cmake \
        bluez-libs-devel \
        pkgconfig

    echo -e "${YELLOW}[*] Instalando herramientas opcionales...${NC}"
    $SUDO yum install -y \
        bluez \
        bluez-tools || true
}

install_arch() {
    echo -e "${YELLOW}[*] Instalando dependencias requeridas...${NC}"
    $SUDO pacman -S --noconfirm \
        base-devel \
        cmake \
        bluez \
        bluez-libs

    echo -e "${YELLOW}[*] Instalando herramientas opcionales...${NC}"
    $SUDO pacman -S --noconfirm \
        bluez-utils \
        rfkill \
        doxygen \
        graphviz || true
}

case "$DISTRO" in
    debian)
        install_debian
        ;;
    fedora)
        install_fedora
        ;;
    rhel)
        install_rhel
        ;;
    arch)
        install_arch
        ;;
    *)
        echo -e "${RED}[ERROR] Distribucion no soportada${NC}"
        echo "Instale manualmente:"
        echo "  - g++ con soporte C++17"
        echo "  - CMake >= 3.14"
        echo "  - libbluetooth-dev / bluez-libs-devel"
        echo "  - pthread"
        exit 1
        ;;
esac

echo ""
echo -e "${GREEN}========================================================${NC}"
echo -e "${GREEN}  Verificando instalacion...${NC}"
echo -e "${GREEN}========================================================${NC}"

check_cmd() {
    if command -v "$1" &>/dev/null; then
        echo -e "  ${GREEN}[OK]${NC} $1 — $($1 --version 2>&1 | head -1)"
    else
        echo -e "  ${RED}[NO]${NC} $1 — NO INSTALADO"
    fi
}

check_cmd g++
check_cmd cmake
check_cmd make

# Verificar librerías
echo ""
echo -e "${YELLOW}Verificando librerias...${NC}"
if ldconfig -p 2>/dev/null | grep -q libbluetooth; then
    echo -e "  ${GREEN}[OK]${NC} libbluetooth — detectada"
else
    echo -e "  ${RED}[NO]${NC} libbluetooth — NO DETECTADA"
fi

# Verificar Bluetooth service
echo ""
echo -e "${YELLOW}Verificando servicio Bluetooth...${NC}"
if systemctl is-active bluetooth &>/dev/null; then
    echo -e "  ${GREEN}[OK]${NC} bluetooth.service — activo"
else
    echo -e "  ${YELLOW}[!]${NC} bluetooth.service — inactivo (ejecute: sudo systemctl start bluetooth)"
fi

echo ""
echo -e "${GREEN}========================================================${NC}"
echo -e "${GREEN}  Instalacion completada${NC}"
echo -e "${GREEN}========================================================${NC}"
echo ""
echo "Para compilar:"
echo "  cd $(dirname "$0")/.."
echo "  make clean && make -j\$(nproc)"
echo "  ./bin/elm327_console"
echo ""
echo "Para permisos Bluetooth:"
echo "  sudo usermod -aG bluetooth \$USER"
echo "  (cerrar sesion y volver a entrar)"
