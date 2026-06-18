# OBD2 Console Skill

This project is a console-only OBD-II scanner application using ELM327 Bluetooth adapter.

## Key Facts

- **Language**: C++17
- **Build**: Makefile or CMake
- **Dependencies**: libbluetooth-dev (bluez), pthread
- **No Qt**: Console-only, no graphical dependencies
- **Platform**: Linux (Bluetooth RFCOMM)

## Common Tasks

### Fix splitResponse parsing
Look in `src/elm327.cpp` for `splitResponse()`. The parser strips `\r\n`, removes prompt `>`, converts to uppercase, then searches for known OBD headers (`41`, `43`, `47`, etc.) using `find()` at any position.

### Add a new PID
1. Add method declaration in `include/elm327.hpp`
2. Implement in `src/elm327.cpp` using `send("PID")` and `splitResponse()`
3. Add menu entry in `include/console_app.hpp` (`printMenu()` + `processOption()`)

### Fix STOPPED recovery
STOPPED detection uses adaptive penalty (`m_stoppedPenaltyMs`). The recovery sequence is:
`wake → ATD → ATZ fallback → flush buffer → reconfigure AT`

### Protocol caching
`ATDPN` result persists to `protocol.cache`. Cache hits skip `ATSP0` (~2-3s saved).

## Build Commands

```bash
make clean && make -j$(nproc)
./bin/elm327_console
```

## Key Files

| File | Purpose |
|------|---------|
| `src/elm327.cpp` | Core ELM327 driver: connection, AT commands, PID parsing, recovery |
| `include/elm327.hpp` | ELM327 class declaration with all PID methods |
| `include/console_app.hpp` | Full menu system with all 41 options inline |
| `src/logger.cpp` | CSV and session logging |

## Known Bugs to Fix

- `write()` return value not checked (warn_unused_result)
- Member initialization order warnings in constructors
- `sendRaw()` and `send()` code duplication (~80% identical)
