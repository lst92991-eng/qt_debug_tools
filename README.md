# MCU Debug Tool

Qt 6 / C++17 MCU debugging application. The existing Physical -> Protocol -> DebugCore -> Visual/Control plugin architecture is retained. See `docs/reliability-validation.md` for the repair scope, automated coverage and hardware acceptance checklist.

## Build and test

Qt 6.2 or newer is required, with Core, Widgets and SerialPort. Tests also require Qt Test. On Linux install the Qt development packages, Ninja and optionally libusb-1.0 development headers. Without libusb the Linux USB plugin builds as an unavailable stub.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The single-config executable is `build/bin/mcd_app` and its plugins are under `build/bin/plugins/`. For Visual Studio or Ninja Multi-Config, both are under `build/bin/<CONFIG>/` instead. Always run a freshly built executable with plugins from the same build.

Windows example (replace `C:/Qt/...` with the local Qt installation):

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DBUILD_TESTING=ON
cmake --build build-vs --config Release --parallel
ctest --test-dir build-vs -C Release --output-on-failure
```

Qt's `bin` directory must be on PATH when running from a development build on Windows. Distributing the application to another PC also requires deploying the matching Qt runtime and platform plugin; this repository is not a standalone installer.

`mcd_app --smoke-test` loads and counts the production plugins, then destroys them before QApplication exits. Test-only mock plugins are built under `test-plugins/`, outside the application's scanned plugin tree.

## Binary compatibility

**The SDK IIDs are now 2.0. Clean-rebuild the application and all plugins together.** Channel identifiers are now 64-bit, and lifecycle methods were added. Old 1.0 plugin binaries are explicitly rejected before instantiation. Do not copy old DLLs/SOs into the new plugin directory.

## Plugins and connection behavior

- Physical: QSerialPort, Linux libusb bulk, Windows WinUSB bulk. WinUSB requires a device bound to WinUSB and its interface GUID. USB timeout configuration is restricted to 1..2000 ms.
- Protocol: raw bytes, or the existing custom `CA FD + ID_BE32 + length_u8 + payload` envelope.
- Visual: raw packet list/details/filter/export, numeric chart and gauge.
- Control: raw HEX commands/presets/periodic send and an SL-format numeric slider.

USB reads and writes execute in a worker thread. Calling `write()` accepts a command into a bounded queue; it does not wait for native USB I/O. A successful enqueue is logged as TX with `tx_state=queued`, **not as a device acknowledgement**. Later transport errors are reported separately. A failed native write may already have transmitted a prefix: the application does not automatically resend the whole command.

Disconnect, transport failure, protocol replacement and plugin rescan stop periodic/live sending. Reconnecting does not re-arm it. Old queued callbacks are invalidated by the transport/core session boundary. Pending commands are discarded on disconnect, not sent to the next device.

Serial configuration values are checked instead of silently substituted. "Serial Port Open Check" only checks local port opening; it is not a peer baud-rate detector.

## CAN envelope compatibility

The wire format is unchanged. The length byte is a payload byte count (0..64), **not the CAN-FD encoded DLC**. This legacy envelope does not carry independent IDE, FD or BRS flags, so the application does not infer missing flags for short frames. IDs are retained as the complete 32-bit envelope field; byte channels use `(ChannelId(id) << 6) | byte_position`.

Raw Viewer retains the entire received envelope, including its header and ID. Payload-only bytes are also available in frame attributes. Slider's `SL` format is not a CAN command and is rejected when the CAN protocol is selected. Raw Control remains an explicit byte-level sender, including when CAN framing is selected.

The envelope has no CRC and cannot perfectly disambiguate all corrupted streams. Changing that protocol requires coordinated firmware changes and is outside this repair pass.

## Bounded histories and persistence

Numeric history defaults to at most 20,000 samples per channel, at most 1,024 channels and at most 1,000,000 retained samples in total. As channels are added, per-channel history windows shrink to fit the aggregate budget. Full histories roll over their oldest samples instead of freezing live input. Chart retention has equivalent channel/aggregate bounds. These are sample-count limits, not a claim about exact process RSS.

`Save Numeric History` writes the version-2 `.mcdr` format with 64-bit IDs; valid version-1 files within the documented limits can still be loaded. Invalid sizes, duplicate channels, truncated records, nonfinite values and unexpected trailing data are rejected without replacing current history. Version-1 channel collisions already present in old files cannot be reconstructed. Atomic file replacement is used for history and session saves.

Loading numeric history requires disconnection, clears/repopulates numeric views, does not send commands or duplicate samples into the store, and does not erase Raw Viewer's retained packet evidence. Channel names/units are stored in session JSON; load the corresponding session when those labels are required alongside numeric history.

Raw Viewer has independent retained and pending limits: each up to 50,000 packets and 8 MiB of payload. Pause freezes the displayed model while buffering up to the pending limit. The UI reports evictions and drops. Details preview at most 4 KiB to avoid a large-packet text-rendering stall. `Export Raw Log` writes full retained/pending packets to JSONL, regardless of the current display filter. It is a bounded-history export, not an unlimited lossless disk recorder.

The gauge remains a demonstration with a 0..100 needle range; its numeric readout is not clipped. The chart is a bounded diagnostic visualization, not a hard-real-time acquisition system.

## Validation limits

GitHub Actions builds and runs the C++/Qt tests on Linux and Windows, including both single- and multi-config output layouts. Linux also has AddressSanitizer/UndefinedBehaviorSanitizer checks; leak detection is disabled for that job and no leak-free claim is made. The actual status of each run is authoritative; adding a workflow does not by itself mean that it passed.

Automated tests use a test-only physical plugin and simulated USB callbacks. They do **not** validate a real serial adapter, WinUSB driver, libusb endpoint, MCU acknowledgement, unplug/replug behavior or sustained device throughput. Perform the hardware acceptance checklist before treating the draft repair as a hardware-qualified release.
