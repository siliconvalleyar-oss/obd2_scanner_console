# Installation

## Requirements

### Linux (Debian/Ubuntu)
```bash
sudo apt install build-essential libbluetooth-dev cmake
```

### Linux (Fedora/RHEL)
```bash
sudo dnf install gcc-c++ bluez-libs-devel cmake
```

## Build

### Makefile (rápido)
```bash
make clean && make -j$(nproc)
./bin/elm327_console
```

### CMake (recomendado)
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./elm327_console
```

## Permissions (Linux Bluetooth)

```bash
sudo usermod -aG bluetooth $USER
# Cerrar sesión y volver a entrar
newgrp bluetooth
```

## First Connection

1. Pair the ELM327 via system Bluetooth
2. Note the MAC address (e.g. `00:1D:A5:07:23:6E`)
3. Run `./bin/elm327_console`
4. Enter the MAC when prompted
5. Use the numbered menu (1-41)

## Troubleshooting

| Problem | Solution |
|---|---|
| `socket: Operation not permitted` | Add user to `bluetooth` group |
| `rfcomm: Cannot open device` | Run `sudo rfcomm release all` or `bash/clear_rfcomm.sh` |
| `connect: Connection refused` | Verify ELM327 is powered and paired |
| `STOPPED` frecuente | Increase sample time in Option 40 |
| `fatal error: bluetooth/bluetooth.h` | Install `libbluetooth-dev` |
