# Architecture

## Project Structure

```
├── CMakeLists.txt          # CMake build system
├── Makefile                # Traditional Makefile
├── include/
│   ├── elm327.hpp          # ELM327 Bluetooth driver
│   ├── console_app.hpp     # Console menu application
│   └── logger.hpp          # CSV and session logging
├── src/
│   ├── main.cpp            # Entry point
│   ├── elm327.cpp          # ELM327 implementation
│   ├── console_app.cpp     # Menu application (extensible)
│   └── logger.cpp          # Logger implementation
├── bash/
│   └── clear_rfcomm.sh     # Bluetooth port cleanup
└── docs/
```

## Data Flow

```
main()
  └── OBD::App::run()
        ├── Logger/SessionLogger init
        ├── ELM327::connectBT()
        │     ├── fullCleanup() → rfcomm release
        │     ├── socket() → connect() → ATZ
        │     ├── ATE0, ATL0, ATS0, ATH0
        │     ├── flush (\r ×3)
        │     ├── ATSP{n} or ATSP0
        │     ├── ATAT1, ATST10, ATAL1
        │     ├── ATCF/ATCM (if protocol 6-9)
        │     └── detectAndCacheProtocol()
        ├── printMenu() loop
        └── processOption(1..41)
```

## Connection Flow

```
fullCleanup() → socket() → connect() → ATZ(2000ms)
  → ATE0, ATL0, ATS0, ATH0 → flush (\r ×3)
  → ATSP{n} or ATSP0 → ATAT1, ATST10, ATAL1
  → ATCF/ATCM (if protocol 6-9)
  → detectAndCacheProtocol()
```

## Error Recovery

STOPPED detection is built into `send()` and `sendRaw()`:
1. Response contains "STOPPED" → adaptive penalty +100ms (max 1000ms)
2. `recoverFromStopped()`: wake → ATD → ATZ fallback → flush → reconfigure
3. After 10 consecutive successes → penalty -25ms (decay to 0)

## Protocol Caching

`ATDPN` result is saved to `protocol.cache` to skip ATSP0 re-detection on reconnect.
