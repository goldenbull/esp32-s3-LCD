# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 LCD driver project built with ESP-IDF v6.0.1. Target chip: `esp32s3`. The entry point is `main/main.c:app_main()`.

## Environment Setup

The ESP-IDF environment must be sourced before running any `idf.py` commands:

```sh
source ~/.espressif/v6.0.1/esp-idf/export.sh
```

## Common Commands

```sh
idf.py build                  # Compile the project
idf.py flash                  # Flash to device (auto-detects port)
idf.py monitor                # Open serial monitor (115200 baud)
idf.py flash monitor          # Flash and immediately open monitor
idf.py menuconfig             # Open interactive configuration UI
idf.py fullclean              # Delete build directory entirely
idf.py -p /dev/ttyUSB0 flash  # Flash to a specific port
```

## Architecture

This is a standard ESP-IDF project structure:

- `main/main.c` — application entry point (`app_main`)
- `main/CMakeLists.txt` — registers source files with `idf_component_register`
- `CMakeLists.txt` — top-level CMake, includes IDF project boilerplate
- `sdkconfig` — generated KConfig settings (committed; tracks hardware configuration choices)
- `build/` — generated build artifacts (gitignored)

Flash layout (from `build/flasher_args.json`):
- `0x0` — bootloader
- `0x8000` — partition table
- `0x10000` — application

## Key Configuration

- Flash: DIO mode, 2 MB, 80 MHz
- OpenOCD (JTAG): `board/esp32s3-builtin.cfg` (uses the ESP32-S3's built-in USB-JTAG interface)
- Clangd for IntelliSense reads `build/compile_commands.json`; the `.clangd` file strips `-f*` and `-m*` flags that clang doesn't understand
- `IDF_PATH` must point to `~/.espressif/v6.0.1/esp-idf`

## Adding New Source Files

Register them in `main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c" "new_file.c"
                    INCLUDE_DIRS ".")
```

## Adding IDF Components

Add dependencies to `main/CMakeLists.txt` via the `REQUIRES` or `PRIV_REQUIRES` argument:

```cmake
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd esp_driver_spi)
```
