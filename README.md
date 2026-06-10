# MCU Debug Tool

Qt 6 / C++17 implementation scaffold for the plugin-based MCU debugging tool described in `2026-06-09-mcu-debug-tool-design.md`.

## Implemented Scope

- Stage 1: CMake project structure and plugin SDK interfaces.
- Stage 2: Core hub with `DebugCore`, `PluginManager`, `ChannelHub`, and `RingBufferPool`.
- Stage 3: P0/P1 plugins:
  - Serial (Generic) physical plugin via `QSerialPort`.
  - USB Raw (Linux) physical plugin via `libusb-1.0` when available.
  - Raw Protocol pass-through parser/encoder.
  - Raw Viewer with RX/TX timestamps, HEX/ASCII view, filtering, pause, clear, and detail dump.
  - Raw Control with HEX validation, history, periodic send, and quick-send presets.
- Stage 4: Main Qt application with plugin scanning, visual/control plugin hosting, and device configuration.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Runtime plugins are placed under `build/bin/plugins/<type>/`, and `mcd_app` scans that directory on startup.

On Linux, install Qt 6 Widgets, Qt 6 SerialPort, and `libusb-1.0` development headers to enable the real USB Raw plugin. Without libusb, the USB plugin target still builds but reports that USB Raw is unavailable.
