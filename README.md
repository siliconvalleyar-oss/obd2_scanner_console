# OBD2 Console Scanner — v1.0.0

Escáner de diagnóstico automotriz OBD-II profesional por consola.
Conexión Bluetooth ELM327 con menú interactivo completo.

## Características

### Diagnóstico del vehículo
- Auto Scan: escaneo completo de módulos CAN (ECU, TCM, ABS, Airbag, BCM, etc.)
- Lectura de sensores: RPM, velocidad, temperatura motor, carga, MAF, presión, avance, combustible
- Sensores de oxígeno: voltaje O2 B1S1-B2S4 con estado pobre/rica/normal y Fuel Trims
- Códigos DTC: leer (Modo 03), borrar (Modo 04), pendientes (Modo 07), permanentes (Modo 0A)
- Freeze Frame (Modo 02) y Monitoreo OBD (Modo 06)
- Información del vehículo: VIN, MIL, protocolo

### Datos en vivo
- Dashboard tiempo real con indicadores de motor
- Gráfico ASCII multiparámetro (RPM, carga, acelerador, MAF)
- Osciloscopio O2 con representación ASCII
- Logger CSV de parámetros del motor
- Log completo de sensores
- Diagnóstico P0171 con duración configurable

### Funciones avanzadas GM
- Kilometraje ECU, temperatura catalizador, presión combustible
- Torque motor, voltaje ECU, temperatura transmisión
- Historial errores GM vía Modo 22

### Servicios especiales
- Información detallada de servicios (Oil Life, EPB, BMS, SAS, DPF, etc.)

## Requisitos

```bash
# Ubuntu/Debian
sudo apt install build-essential libbluetooth-dev cmake
```

## Compilación

### Makefile
```bash
make clean && make -j$(nproc)
./bin/elm327_console
```

### CMake
```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
./elm327_console
```

## Uso

```bash
./bin/elm327_console
```

Ingrese la dirección MAC del ELM327 cuando se solicite. Luego seleccione opciones del menú numerado (1-41).

## Conexión Bluetooth

1. Empareje el ELM327 vía Bluetooth del sistema
2. Anote la dirección MAC (ej: `00:1D:A5:07:23:6E`)
3. Ejecute `./bin/elm327_console`
4. Ingrese la MAC cuando se solicite

## Solución de problemas

- **No conecta**: Verifique Bluetooth encendido, MAC correcta, y ELM327 encendido
- **STOPPED frecuente**: Aumente tiempo de muestreo en Opción 40 (Configuración)
- **Permisos Linux**: Agregue usuario al grupo `bluetooth` o ejecute con `sudo`
- **Puerto ocupado**: Use `bash/clear_rfcomm.sh` antes de conectar
