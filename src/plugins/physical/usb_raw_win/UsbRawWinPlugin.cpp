#include "UsbRawWinPlugin.h"
#include <QElapsedTimer>
#if defined(Q_OS_WIN)
#include <setupapi.h>
#include <objbase.h>
namespace {
QString windowsError(const QString& prefix, DWORD error)
{
    LPWSTR buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    const QString message = buffer ? QString::fromWCharArray(buffer).trimmed() : QString::number(error);
    if (buffer) LocalFree(buffer);
    return prefix + QStringLiteral(": ") + message;
}
QString findDevicePath(const GUID& guid, int vid, int pid)
{
    const auto info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return {};
    QString found;
    for (DWORD index = 0; found.isEmpty(); ++index) {
        SP_DEVICE_INTERFACE_DATA data = {};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &guid, index, &data)) break;
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &data, nullptr, 0, &size, nullptr);
        if (size == 0) continue;
        QByteArray storage(size, '\0');
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &data, detail, size, nullptr, nullptr)) continue;
        const auto path = QString::fromWCharArray(detail->DevicePath);
        const auto lower = path.toLower();
        if (lower.contains(QStringLiteral("vid_%1").arg(vid, 4, 16, QLatin1Char('0')))
            && lower.contains(QStringLiteral("pid_%1").arg(pid, 4, 16, QLatin1Char('0')))) found = path;
    }
    SetupDiDestroyDeviceInfoList(info);
    return found;
}
}
#endif
UsbRawWinPlugin::UsbRawWinPlugin(QObject* parent) : IPhysicalPlugin(parent), m_io(this) {}
UsbRawWinPlugin::~UsbRawWinPlugin() { close(); }
bool UsbRawWinPlugin::open(const QVariantMap& config)
{
    close();
#if defined(Q_OS_WIN)
    const auto defaults = defaultConfig();
    const auto value = [&](const QString& key) { return config.value(key, defaults.value(key)); };
    GUID guid = {};
    const QString guidText = value("device_guid").toString().trimmed();
    if (guidText.isEmpty() || CLSIDFromString(reinterpret_cast<LPCOLESTR>(guidText.utf16()), &guid) != S_OK) {
        emit errorOccurred(tr("A valid WinUSB device interface GUID is required")); return false;
    }
    const int vid = parseHexInt(value("vid"), -1), pid = parseHexInt(value("pid"), -1);
    m_epOut = parseHexInt(value("ep_out"), -1);
    m_epIn = parseHexInt(value("ep_in"), -1);
    bool ok = false;
    const int timeout = value("timeout_ms").toInt(&ok);
    if (vid <= 0 || vid > 65535 || pid <= 0 || pid > 65535 || m_epOut < 1 || m_epOut > 15
        || m_epIn < 129 || m_epIn > 143 || !ok || timeout < 1 || timeout > 2000) {
        emit errorOccurred(tr("Invalid USB configuration (timeout must be 1..2000 ms)")); return false;
    }
    m_timeoutMs = static_cast<unsigned long>(timeout);
    const auto path = findDevicePath(guid, vid, pid);
    if (path.isEmpty()) { emit errorOccurred(tr("No matching WinUSB device interface")); return false; }
    m_device = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (m_device == INVALID_HANDLE_VALUE) { const DWORD error = GetLastError(); emit errorOccurred(windowsError(tr("CreateFile failed"), error)); return false; }
    if (!WinUsb_Initialize(m_device, &m_usb)) {
        const DWORD error = GetLastError(); emit errorOccurred(windowsError(tr("WinUSB initialization failed"), error)); close(); return false;
    }
    unsigned long readTimeout = 50;
    if (!WinUsb_SetPipePolicy(m_usb, UCHAR(m_epIn), PIPE_TRANSFER_TIMEOUT, sizeof(readTimeout), &readTimeout)
        || !WinUsb_SetPipePolicy(m_usb, UCHAR(m_epOut), PIPE_TRANSFER_TIMEOUT, sizeof(m_timeoutMs), &m_timeoutMs)) {
        const DWORD error = GetLastError(); emit errorOccurred(windowsError(tr("Cannot configure USB pipe timeouts"), error)); close(); return false;
    }
    m_open = true;
    m_io.start(
        [this]() -> UsbIoWorker::ReadResult {
            QByteArray bytes(4096, '\0');
            ULONG transferred = 0;
            const BOOL ok = WinUsb_ReadPipe(m_usb, UCHAR(m_epIn), reinterpret_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()), &transferred, nullptr);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            if (ok) {
                if (transferred > ULONG(bytes.size())) return {{}, tr("Invalid WinUSB read length")};
                bytes.resize(qsizetype(transferred));
                return {bytes, {}}; // Zero-length success is valid, not an error.
            }
            if (error == ERROR_SEM_TIMEOUT || (error == ERROR_OPERATION_ABORTED && !m_io.running())) return {};
            return {{}, windowsError(tr("WinUSB read failed"), error)};
        },
        [this](const QByteArray& bytes, const std::atomic_bool& running) -> QString {
            qsizetype offset = 0;
            QElapsedTimer deadline;
            deadline.start();
            while (offset < bytes.size() && running) {
                if (deadline.elapsed() >= m_timeoutMs) return tr("WinUSB write deadline expired; delivery may be partial");
                ULONG transferred = 0;
                const BOOL ok = WinUsb_WritePipe(m_usb, UCHAR(m_epOut),
                    reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.constData() + offset)), ULONG(bytes.size() - offset), &transferred, nullptr);
                const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
                // After a failed WinUSB write the delivered prefix is not assumed known; do not retry it.
                if (!ok) return windowsError(tr("WinUSB write failed; delivery may be partial, command not retried"), error);
                if (transferred == 0 || transferred > ULONG(bytes.size() - offset)) return tr("Invalid WinUSB write progress");
                offset += transferred;
            }
            return {};
        },
        [this](const QByteArray& bytes) { emit dataReceived(bytes); },
        [this](const QString& error) { emit errorOccurred(error); },
        [this]() { if (m_open) { m_open = false; emit statusChanged(false); } });
    emit statusChanged(true);
    return true;
#else
    Q_UNUSED(config)
    emit errorOccurred(tr("USB Raw (Windows) is only available on Windows"));
    return false;
#endif
}
void UsbRawWinPlugin::close()
{
    m_io.requestStop();
#if defined(Q_OS_WIN)
    if (m_usb) { WinUsb_AbortPipe(m_usb, UCHAR(m_epIn)); WinUsb_AbortPipe(m_usb, UCHAR(m_epOut)); }
#endif
    m_io.stop(); // Join before native handles or plugin code are released.
#if defined(Q_OS_WIN)
    if (m_usb) { WinUsb_Free(m_usb); m_usb = nullptr; }
    if (m_device != INVALID_HANDLE_VALUE) { CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE; }
#endif
    if (m_open) { m_open = false; emit statusChanged(false); }
}
bool UsbRawWinPlugin::isOpen() const { return m_open && m_io.running(); }
qint64 UsbRawWinPlugin::write(const QByteArray& bytes)
{
    if (!isOpen()) return -1;
    const auto accepted = m_io.enqueue(bytes);
    if (accepted < 0) emit errorOccurred(tr("USB TX queue full or command exceeds 64 KiB"));
    return accepted;
}
QString UsbRawWinPlugin::name() const { return QStringLiteral("USB Raw (Windows)"); }
QString UsbRawWinPlugin::version() const { return QStringLiteral("2.0.0"); }
QVariantMap UsbRawWinPlugin::defaultConfig() const
{
    return {{"device_guid", ""}, {"vid", "0x0483"}, {"pid", "0x5740"}, {"ep_out", "0x01"}, {"ep_in", "0x81"}, {"timeout_ms", 1000}};
}
int UsbRawWinPlugin::parseHexInt(const QVariant& value, int fallback)
{
    bool ok = false;
    const auto text = value.toString().trimmed();
    int result = text.toInt(&ok, 0);
    if (!ok) result = text.toInt(&ok, 16);
    return ok ? result : fallback;
}
