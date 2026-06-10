# MCU Debug Tool — Architecture Design

**Date:** 2026-06-09
**Status:** Approved
**Tech Stack:** Qt 6.x / C++17, QPluginLoader, libusb, QSerialPort

---

## 1. Overview

A plugin-based microcontroller debugging tool for bidirectional data I/O and time-series visualization. The physical layer communicates with devices via serial or USB raw; protocol plugins parse raw bytes into structured channel data; visualization plugins render it as time-series views; control plugins send commands back to the device.

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture | Central Hub (DebugCore) | Simpler debugging, clear data ownership, plugins are stateless |
| Plugin language | C++ dynamic libraries (.dll/.so) | Maximum performance, direct Qt integration via QPluginLoader |
| Thread model | Single I/O thread + UI main thread | One device per process; multi-device = multiple instances |
| Data model | Per-channel ring buffer, numeric index | Compact, time-series-centric, sliding window for history |
| Cross-platform | Physical plugins encapsulate OS differences | "serial_generic", "usb_raw_linux", "usb_raw_win" |
| Plugin depth | Full: physical, protocol, visual, control | All four layers are replaceable plugins |

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      Qt MainWindow                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐     │
│  │ Raw View │  │  Chart   │  │  Gauge   │  │  Widget  │     │
│  │ Plugin   │  │ Plugin   │  │ Plugin   │  │ Plugin   │     │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘     │
│       │ subscribe   │ subscribe   │ subscribe   │ cmd        │
│       ▼             ▼             ▼             │            │
│  ┌──────────────────────────────────────────────┴────────┐  │
│  │                  DebugCore (Hub)                       │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │  │
│  │  │Plugin Mgr│  │ChannelHub│  │Ring Buffer Pool  │    │  │
│  │  └──────────┘  └──────────┘  └──────────────────┘    │  │
│  └──────────────────────┬───────────────────────────────┘  │
│                         │                                   │
│       ┌─────────────────┼─────────────────┐                 │
│       ▼                 ▼                 ▼                 │
│  ┌──────────┐     ┌──────────┐     ┌──────────┐            │
│  │ Protocol │     │ Protocol │     │ Protocol │            │
│  │ Plugin A │     │ Plugin B │     │ Plugin C │            │
│  └────┬─────┘     └────┬─────┘     └────┬─────┘            │
│       │ raw            │ raw            │ raw               │
│       ▼                ▼                ▼                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Physical Layer Plugins                   │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │   │
│  │  │ Serial   │  │ USB Raw  │  │ USB Raw (Win)    │    │   │
│  │  │(generic) │  │ (Linux)  │  │ (future)         │    │   │
│  │  └──────────┘  └──────────┘  └──────────────────┘    │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### Data Flow (Read Path)

```
Device → I/O Thread → Main Thread → Hub → Subscribers

1. Physical plugin (I/O thread): emits dataReceived(QByteArray)
2. Signal crosses to main thread (Qt::QueuedConnection)
3. Protocol plugin: feedBytes(raw) → frameParsed(DataFrame)
4. DebugCore::publish(frame):
   - Push each channel sample into RingBufferPool
   - Fan-out to all IVisualPlugin subscribers with matching channel subscriptions
```

### Control Flow (Write Path)

```
Widget → Hub → Protocol → Physical → Device

1. IControlPlugin emits commandGenerated(QVariantMap cmd)
2. DebugCore::sendCommand(cmd)
3. Active IProtocolPlugin::encodeCommand(cmd) → QByteArray
4. Active IPhysicalPlugin::write(bytes) → device
```

---

## 3. Plugin SDK — Five Core Interfaces

### 3.1 IPhysicalPlugin

```cpp
class IPhysicalPlugin : public QObject {
    Q_OBJECT
public:
    virtual ~IPhysicalPlugin() = default;

    // Lifecycle
    virtual bool open(const QVariantMap& config) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // I/O
    virtual qint64 write(const QByteArray& data) = 0;

    // Metadata
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QVariantMap defaultConfig() const = 0;

signals:
    void dataReceived(const QByteArray& rawBytes);
    void errorOccurred(const QString& message);
    void statusChanged(bool connected);
};
```

### 3.2 IProtocolPlugin

```cpp
struct ChannelSample {
    quint16 index;   // Numeric channel identifier
    double value;    // Numeric value (NaN = raw-only channel)
};

struct DataFrame {
    qint64 timestamp_us;              // Microsecond timestamp
    QVector<ChannelSample> channels;  // One or more channel samples
    QByteArray rawPayload;            // Optional: raw binary payload
};

class IProtocolPlugin : public QObject {
    Q_OBJECT
public:
    virtual ~IProtocolPlugin() = default;

    // Inbound: raw bytes → parsed frames
    virtual void feedBytes(const QByteArray& raw) = 0;

    // Outbound: command → raw bytes
    virtual QByteArray encodeCommand(const QVariantMap& command) = 0;

    virtual QString name() const = 0;
    virtual QString version() const = 0;

signals:
    void frameParsed(const DataFrame& frame);
};
```

### 3.3 IVisualPlugin

```cpp
class IVisualPlugin : public QWidget {
    Q_OBJECT
public:
    virtual ~IVisualPlugin() = default;

    // Called by Hub when subscribed channels have new data
    virtual void onChannelData(const DataFrame& frame) = 0;

    // Which channels this visualizer cares about
    // Empty list = subscribe to all (raw viewer use case)
    virtual QList<quint16> subscribedChannels() = 0;

    // Request historical data replay from this timestamp (0 = no history)
    virtual qint64 historyFrom() = 0;

    virtual QString name() const = 0;
};
```

### 3.4 IControlPlugin

```cpp
class IControlPlugin : public QWidget {
    Q_OBJECT
public:
    virtual ~IControlPlugin() = default;
    virtual QString name() const = 0;

signals:
    void commandGenerated(const QVariantMap& command);
    // command map keys: "bytes" (QByteArray), "period_ms" (int, optional)
};
```

### 3.5 Plugin JSON Metadata

```json
{
  "type": "physical",
  "name": "USB Raw (Linux)",
  "version": "1.0.0",
  "interface": "IPhysicalPlugin",
  "author": "...",
  "description": "Raw USB bulk transfer using libusb",
  "platforms": ["linux"],
  "priority": 10
}
```

Plugin types: `"physical"`, `"protocol"`, `"visual"`, `"control"`

---

## 4. Core Components

### 4.1 DebugCore (Hub)

Singleton. Owns PluginManager, ChannelHub, and RingBufferPool. All inter-plugin communication flows through it.

```cpp
class DebugCore : public QObject {
    Q_OBJECT
public:
    static DebugCore* instance();

    // Plugin lifecycle
    PluginManager* pluginManager();

    // Data publishing
    void publish(const DataFrame& frame);

    // Control commands
    void sendCommand(const QVariantMap& command);

    // Channel access
    ChannelHub* channelHub();
    RingBufferPool* ringBufferPool();

private:
    DebugCore() = default;
    PluginManager* m_pluginMgr;
    ChannelHub* m_channelHub;
    RingBufferPool* m_ringPool;
};
```

### 4.2 RingBufferPool

Sliding-window ring buffers, one per channel index. Supports "collect first, visualize later" — plugins can attach after data collection and replay history.

```cpp
struct TimedSample {
    qint64 timestamp_us;
    double value;
};

struct RingBuffer {
    QVector<TimedSample> data;  // Pre-allocated circular storage
    int head = 0;               // Write cursor
    int capacity = 1'000'000;   // Default ~16 MB per channel
    qint64 oldest_ts = 0;       // Left edge of sliding window
    int sampleCount = 0;        // Total samples written (for occupancy)

    void push(TimedSample s);
    QVector<TimedSample> range(qint64 from_us, qint64 to_us) const;
};

class RingBufferPool {
public:
    void push(quint16 channelIdx, TimedSample sample);
    QVector<TimedSample> replay(quint16 channelIdx, qint64 from_us);
    qint64 newestTimestamp(quint16 channelIdx) const;
    QList<quint16> activeChannels() const;

private:
    QHash<quint16, RingBuffer> m_buffers;
    mutable QReadWriteLock m_lock;
};
```

### 4.3 ChannelHub

Manages pub/sub for channel data. Bridges protocol output to visualizer inputs.

```cpp
class ChannelHub : public QObject {
    Q_OBJECT
public:
    void subscribe(IVisualPlugin* plugin, const QList<quint16>& channels);
    void unsubscribe(IVisualPlugin* plugin);
    void dispatch(const DataFrame& frame);

private:
    // channelIndex → list of plugins
    QHash<quint16, QSet<IVisualPlugin*>> m_subscriptions;
    QSet<IVisualPlugin*> m_wildcardSubscribers; // subscribe to all channels
};
```

### 4.4 PluginManager

Discovers, loads, and manages plugin lifecycle.

```cpp
class PluginManager : public QObject {
    Q_OBJECT
public:
    void scanPlugins(const QString& pluginDir);

    // Per-type access
    QList<IPhysicalPlugin*> physicalPlugins() const;
    QList<IProtocolPlugin*> protocolPlugins() const;
    QList<IVisualPlugin*> visualPlugins() const;
    QList<IControlPlugin*> controlPlugins() const;

    // Active plugin selection
    bool activatePhysical(const QString& name, const QVariantMap& config);
    bool activateProtocol(const QString& name);
    void deactivateAll();

    IPhysicalPlugin* activePhysical() const;
    IProtocolPlugin* activeProtocol() const;

signals:
    void physicalActivated(IPhysicalPlugin* plugin);
    void physicalDeactivated();
    void protocolActivated(IProtocolPlugin* plugin);
    void errorOccurred(const QString& message);

private:
    QPluginLoader* loadPlugin(const QString& path, const QJsonObject& meta);
};
```

---

## 5. Thread Model

```
┌──────────────────────┐     ┌──────────────────────────┐
│     I/O Thread        │     │      Main Thread (UI)     │
│                        │     │                           │
│  PhysicalPlugin        │     │  ProtocolPlugin           │
│    read loop ──────────│────▶│    feedBytes()            │
│                        │ Qt::│    emit frameParsed()     │
│                        │Queued│         │                │
│                        │Conn. │         ▼                │
│  PhysicalPlugin        │     │  DebugCore::publish()     │
│    ◀── write() ────────│─────│    ├─ RingBuffer.push()   │
│                        │     │    └─ ChannelHub.dispatch │
│                        │     │         │                 │
│                        │     │         ▼                 │
│                        │     │  IVisualPlugin::          │
│                        │     │    onChannelData()        │
│                        │     │                           │
│                        │     │  IControlPlugin::         │
│                        │     │    commandGenerated()     │
│                        │     │         │                 │
│                        │     │         ▼                 │
│                        │     │  DebugCore::sendCommand() │
│                        │     │  Protocol::encodeCommand  │
│                        │     │         │                 │
│                        │     │  Physical::write() ───────│──▶
└──────────────────────┘     └──────────────────────────┘
```

- Physical plugin runs in a dedicated I/O thread; its `dataReceived()` signal crosses to main thread via `Qt::QueuedConnection`
- All other plugins (protocol, visual, control) run on the main thread
- RingBufferPool uses `QReadWriteLock` for thread-safe access from I/O and UI threads
- One process = one device; for multi-device, open multiple tool instances

---

## 6. Plugin Directory Structure

```
plugins/
├── physical/
│   ├── serial_generic.dll        + serial_generic.json
│   ├── usb_raw_linux.so          + usb_raw_linux.json
│   └── usb_raw_win.dll           + usb_raw_win.json        (future)
├── protocol/
│   ├── raw_passthrough.dll       + raw_passthrough.json
│   └── can_frame.dll             + can_frame.json          (future)
├── visual/
│   ├── raw_viewer.dll            + raw_viewer.json
│   ├── time_chart.dll            + time_chart.json         (future)
│   └── gauge.dll                 + gauge.json              (future)
└── control/
    ├── raw_control.dll           + raw_control.json
    └── slider_widget.dll         + slider_widget.json      (future)
```

---

## 7. V1 Scope

### 7.1 Goal

FD CAN robot tuning via USB Raw on Linux. Core send/receive with raw data display and hex input.

### 7.2 P0 (Must Have)

| Module | Component | Description |
|--------|-----------|-------------|
| Physical | USB Raw (Linux) | libusb bulk transfer, configurable VID/PID/endpoint |
| Physical | Serial (Generic) | QSerialPort wrapper, configurable baud/data/stop/parity |
| Protocol | Raw Protocol | Pass-through: raw bytes → DataFrame(ch0, rawPayload) |
| Visual | Raw Viewer | HEX + ASCII dual-column, RX/TX direction, timestamps, auto-scroll, pause/clear |
| Control | Raw Control | HEX input field, single send, hex validation |
| Core | DebugCore | Hub with plugin manager, channel hub, ring buffer pool |
| Core | PluginManager | JSON metadata scan, QPluginLoader, type-based query |
| Core | RingBufferPool | Per-channel sliding window, history replay |

### 7.3 P1 (Important)

- Raw Control: periodic send with configurable interval
- Raw Control: quick-send preset list (saved commands)
- Device configuration panel (connect/disconnect UI)
- Raw Viewer: selected-row hexdump detail pane

### 7.4 P2 (Future)

- USB Raw (Windows)
- CAN frame protocol plugin
- Time-series chart visualization
- Gauge visualization
- Slider/widget control panel

---

## 8. V1 Component Specifications

### 8.1 Raw Viewer

Classic hexdump-style display. Each received or sent packet occupies a row.

**Columns:**
1. Timestamp (microsecond resolution, formatted as `HH:MM:SS.mmmuuu`)
2. Direction indicator (`RX` blue / `TX` green)
3. HEX bytes (space-separated, truncated at ~32 bytes with "..." overflow)
4. Click to expand full hexdump in detail pane

**Controls:**
- `[AutoScroll]` toggle — follow new data or freeze position
- `[Pause]` — halt display updates (data still buffered in ring)
- `[Clear]` — clear visible log
- Filter input — show only packets containing hex pattern

**Performance:** Handle 10,000 packets/second without UI lag. Use QPlainTextEdit with batching (update at most every 16ms).

### 8.2 Raw Control

HEX input field for sending arbitrary binary data to the device.

**Input:**
- Free-form hex entry (spaces/commas optional, e.g., `A5 01 7F` or `A5017F`)
- Real-time validation: invalid hex chars highlighted red
- Enter key = send
- Input history (up/down arrow keys)

**Send modes:**
- Single send button
- Periodic send: interval spinner (ms) + start/stop toggle

**Quick Send:**
- Named presets stored as JSON array: `[{"name": "Ping", "hex": "A50001FF"}, ...]`
- Click preset → load into input field
- Right-click preset → send immediately

### 8.3 USB Raw Plugin (Linux)

```cpp
class UsbRawLinuxPlugin : public IPhysicalPlugin {
    // Uses libusb 1.0
    // Config: { "vid": "0x0483", "pid": "0x5740",
    //            "ep_out": "0x01", "ep_in": "0x81" }
    //
    // Connects via libusb_open_device_with_vid_pid()
    // Claims interface, then loops in I/O thread:
    //   libusb_bulk_transfer(ep_in) → dataReceived()
    // Writes via libusb_bulk_transfer(ep_out)
};
```

**Key configurable parameters:**
- VID, PID (hex strings)
- OUT endpoint address (hex)
- IN endpoint address (hex)
- Transfer timeout (ms, default 1000)

### 8.4 Serial Plugin (Generic)

```cpp
class SerialGenericPlugin : public IPhysicalPlugin {
    // Wraps QSerialPort
    // Config: { "port": "COM3"|"/dev/ttyUSB0", "baud": 115200,
    //           "data_bits": 8, "stop_bits": 1, "parity": "none",
    //           "flow_control": "none" }
    //
    // Connects QSerialPort::readyRead → dataReceived()
    // Writes via QSerialPort::write()
};
```

---

## 9. Error Handling Strategy

- **Physical layer errors** (disconnect, USB unplug): emit `errorOccurred()` signal. Hub notifies all visual plugins. UI shows status bar alert. Ring buffer data preserved for post-mortem analysis.
- **Protocol parse errors**: protocol plugin can emit `frameParsed()` with an error flag or emit a separate `parseError()` signal. Raw protocol never fails (pure pass-through).
- **Plugin load failures**: PluginManager logs error, skips plugin, continues loading remaining plugins. User notified in plugin management UI.
- **Buffer overflow protection**: RingBuffer silently overwrites oldest data when capacity reached. `oldest_ts` advances.

---

## 10. Directory Structure (Source)

```
qt_tool/
├── docs/superpowers/specs/2026-06-09-mcu-debug-tool-design.md  ← this file
├── src/
│   ├── core/
│   │   ├── DebugCore.h/cpp
│   │   ├── PluginManager.h/cpp
│   │   ├── ChannelHub.h/cpp
│   │   └── RingBufferPool.h/cpp
│   ├── sdk/
│   │   ├── IPhysicalPlugin.h
│   │   ├── IProtocolPlugin.h
│   │   ├── IVisualPlugin.h
│   │   ├── IControlPlugin.h
│   │   └── DataFrame.h
│   ├── app/
│   │   ├── MainWindow.h/cpp
│   │   ├── MainWindow.ui
│   │   └── DeviceConfigDialog.h/cpp
│   └── plugins/
│       ├── physical/
│       │   ├── serial_generic/
│       │   └── usb_raw_linux/
│       ├── protocol/
│       │   └── raw_passthrough/
│       ├── visual/
│       │   └── raw_viewer/
│       └── control/
│           └── raw_control/
├── CMakeLists.txt
└── plugins/                    ← build output + metadata
    ├── physical/
    ├── protocol/
    ├── visual/
    └── control/
```

---

## 11. Build System

CMake with Qt6:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MCUDebugTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort)

# Core library (linked by app + all plugins)
add_library(mcd_core STATIC
    src/core/DebugCore.cpp
    src/core/PluginManager.cpp
    src/core/ChannelHub.cpp
    src/core/RingBufferPool.cpp
)

# SDK headers only (included by plugins)
add_library(mcd_sdk INTERFACE)
target_include_directories(mcd_sdk INTERFACE src/sdk)
target_link_libraries(mcd_sdk INTERFACE Qt6::Core Qt6::Widgets)

# Main application
add_executable(mcd_app src/app/main.cpp src/app/MainWindow.cpp)
target_link_libraries(mcd_app PRIVATE mcd_core mcd_sdk Qt6::SerialPort)

# Plugins (each built as shared library)
# ...
```

---

## 12. Future Roadmap (beyond V1)

| Phase | Scope |
|-------|-------|
| V1.1 | Periodic send, quick-send presets, device config panel |
| V1.2 | USB Raw (Windows), Serial baud scan |
| V2 | CAN frame protocol, time-series chart, ring buffer disk persistence |
| V3 | Gauge panel, slider widget, channel naming/mapping, session save/load |
