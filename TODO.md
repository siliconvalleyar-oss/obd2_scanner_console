# TODO

## High Priority
- [ ] Replace blocking `write()` return-value warnings (`[[nodiscard]]`)
- [ ] Add proper error handling for `readModule()` when module doesn't respond
- [ ] Fix member initialization order warnings in headers

## Medium Priority
- [ ] Implement multi-PID aggregate requests to reduce STOPPED risk
- [ ] Add reconnection logic when Bluetooth disconnects
- [ ] Support for CAN FD (ISO 15765-2)
- [ ] Add `-h`/`--help` CLI flag and `--demo` flag
- [ ] Detect Bluetooth adapter before connection attempt

## Low Priority
- [ ] Add unit tests for `splitResponse()`, PID decoders, DTC parsing
- [ ] Add protocol.cache to `.gitignore`
- [ ] Configurable PID selection
- [ ] Windows MSYS2 support

## Known Issues
- `sendRaw()` and `send()` share ~80% identical select()/read code (duplication)
- Service options 21-28 are informational stubs only
