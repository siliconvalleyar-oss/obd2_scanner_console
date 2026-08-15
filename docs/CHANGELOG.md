# Changelog

## v1.0.0 (2026-06-18)

### Added
- Initial console-only OBD2 scanner application
- Full menu system with 41 diagnostic options
- Bluetooth ELM327 connection with AT command negotiation
- All OBD-II PIDs: RPM, speed, coolant, load, throttle, MAF, etc.
- Fuel trims (STFT/LTFT) and oxygen sensors
- DTC read (Modo 03), clear (Modo 04), pending (Modo 07), permanent (Modo 0A)
- Freeze Frame (Modo 02) and OBD Monitoring (Modo 06)
- Auto-Scan: multi-module CAN bus scan (ECU, TCM, ABS, Airbag, BCM)
- ASCII dashboard, graph, and O2 oscilloscope
- CSV logging for motor parameters and full sensor data
- P0171 lean mixture diagnostic with configurable duration
- GM advanced functions: mileage, catalyst temp, fuel pressure, torque, voltage
- Adaptive STOPPED recovery with penalty decay
- Protocol caching via protocol.cache file
- ATH0 (Headers OFF) on all connection paths
- Conditional ATCF/ATCM (CAN filter only for protocols 6-9)
- Buffer flush after ATZ to clear residual data
- Session logging with detailed command/response tracking
