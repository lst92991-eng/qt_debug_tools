# Reliability repair and acceptance

## Scope

This branch repairs the existing application rather than replacing its plugin architecture. The most substantial local implementation changes are the USB I/O worker and Raw Viewer's list model. MainWindow, DebugCore and plugin lifecycle methods are updated to connect these changes safely.

## Automated regression map

| Area | Coverage |
| --- | --- |
| CAN parser | Every split boundary; noise followed by split header; reset after incomplete frame; full-ID channel uniqueness; 64-byte payload |
| Commands | Slider uint8 bounds; configuration changes do not send; unsupported slider/CAN combinations and malformed/oversized CAN commands rejected |
| Session lifecycle | Initial packet emitted inside open; queued data from old connection ignored; failed open followed by successful reopen; periodic sending requires explicit re-arm |
| Plugin lifetime | Repeated unload/rescan; subscriber deletion during dispatch; QWidget plugins destroyed before QApplication; SDK-1 binaries rejected by the loader |
| Persistence | v1 import; wide-ID v2 round trip; invalid capacity/count, duplicate IDs, truncated records, NaN, negative timestamps and trailing junk rejected transactionally |
| Session JSON | Required types, schema version, size limit and 64-bit ID validation; failed parse leaves existing state unchanged |
| Histories/views | Aggregate quota rolls forward; channel bounds; replay does not transmit or duplicate history; numeric replay preserves packet evidence; clear resets numeric and raw views |
| Raw log model | Pause/resume ordering; filter/selection/detail correspondence; 50,000-row rollover without data/index aliasing |
| USB worker simulation | TX budget includes in-flight bytes; enqueue does not wait for native write; stale callbacks are invalidated; zero-length reads are not disconnects; failed writes are not retried; RX overflow is explicit |

The CTest targets are `mcd_tests`, `mcd_sessions`, `mcd_storage`, `mcd_usb_worker`, and `plugins_smoke`. See the source test methods and CI logs for the exact executed assertions. Items enforced by code but not explicitly exercised (for example loading a separately built SDK-1 binary) must not be represented as independent passing test cases.

## Hardware acceptance — not executed by cloud CI

Use a current-limited/safe test setup and harmless test commands; disable actuators or power stages when testing reconnect behavior.

1. Build the branch with the same Qt kit used on the target PC. Remove stale plugin binaries, open every plugin page, save/load a session containing a channel ID above 65535, and rescan repeatedly.
2. Serial: exchange known byte patterns; test valid and invalid port/baud/parity settings; unplug during receiving and periodic sending. Confirm disconnected status, no queued automatic commands after reconnect, and explicit re-arming of periodic/live controls.
3. Linux USB: confirm configured interface/endpoints, permissions and kernel-driver restoration. Exercise short packets, sustained input, partial read timeouts, partial write errors, unplug/replug and close while idle/active.
4. Windows USB: confirm the real WinUSB GUID/endpoints and finite pipe policies. Exercise empty/short packets, failed writes, cancellation/AbortPipe while closing, unplug/replug and no automatic replay of uncertain-delivery commands.
5. Compare device-side sequence numbers or a known byte stream against the exported raw log at the intended throughput. Check queue/retention warnings rather than interpreting a displayed TX entry as a device ACK.
6. Soak test at the intended channel count and input rate. Record responsiveness, memory high-water mark, drop/eviction counters and behavior when history budgets are reached. Test maximum supported history import on the actual PC; import/replay and file dialogs are synchronous UI operations.

A green build/test run is not a substitute for these hardware observations. No production-safety certification, guaranteed native-driver cancellation deadline or lossless unlimited recording is claimed.

## Compatibility notes

- SDK 2.0 requires a clean rebuild of the app and all plugins.
- `.mcdr` remains numeric history; raw packet evidence has a separate JSONL export.
- Old numeric IDs already corrupted by the v1 channel collision cannot be recovered from the file alone.
- The legacy CAN byte envelope is preserved and has no CRC/IDE/FD/BRS fields. A new transport protocol is a separate coordinated firmware/application change.
- No merge to `main` or device-side execution is part of this repair task.
