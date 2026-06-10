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
- Stage 4: P2/Future plugins and app features:
  - USB Raw (Windows) physical plugin via WinUSB device interface GUID.
  - CAN Frame protocol plugin using `CA FD + id + dlc + payload` framing.
  - Time Chart and Gauge visual plugins for numeric channels.
  - Slider Widget control plugin for numeric commands.
  - Session save/load, plugin rescan, channel metadata, channel map editing, serial baud scan, and ring-buffer history persistence.
- Stage 5: Main Qt application with plugin scanning, visual/control plugin hosting, device configuration, and history/session menus.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Runtime plugins are placed under `build/bin/plugins/<type>/`, and `mcd_app` scans that directory on startup.

On Linux, install Qt 6 Widgets, Qt 6 SerialPort, and `libusb-1.0` development headers to enable the USB Raw Linux plugin. Without libusb, the USB plugin target still builds but reports that USB Raw is unavailable.

On Windows, USB Raw requires a device already bound to WinUSB and a configured device interface GUID. VID/PID and bulk endpoint addresses can be set from the device configuration dialog.

On this Windows setup the verified build command is:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build-vs18 -G 'Visual Studio 18 2026' -A x64 -DCMAKE_PREFIX_PATH=D:/Qt/6.9.3/msvc2022_64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build-vs18 --config Release --parallel
.\build-vs18\bin\Release\mcd_app.exe --smoke-test
```
