#include "UsbRawLinuxPlugin.h"
#include <QElapsedTimer>
#include <algorithm>
UsbRawLinuxPlugin::UsbRawLinuxPlugin(QObject* parent) : IPhysicalPlugin(parent), m_io(this) {}
UsbRawLinuxPlugin::~UsbRawLinuxPlugin() { close(); }
bool UsbRawLinuxPlugin::open(const QVariantMap& config)
{
    close();
#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    const auto defaults = defaultConfig();
    const auto value = [&](const QString& key) { return config.value(key, defaults.value(key)); };
    const int vid = parseHexInt(value("vid"), -1);
    const int pid = parseHexInt(value("pid"), -1);
    m_epOut = parseHexInt(value("ep_out"), -1);
    m_epIn = parseHexInt(value("ep_in"), -1);
    bool timeoutOk = false, interfaceOk = false;
    m_timeoutMs = value("timeout_ms").toInt(&timeoutOk);
    m_interfaceNumber = value("interface").toInt(&interfaceOk);
    if (vid <= 0 || vid > 65535 || pid <= 0 || pid > 65535
        || m_epOut < 1 || m_epOut > 15 || m_epIn < 129 || m_epIn > 143
        || !timeoutOk || m_timeoutMs < 1 || m_timeoutMs > 2000
        || !interfaceOk || m_interfaceNumber < 0 || m_interfaceNumber > 255) {
        emit errorOccurred(tr("Invalid USB configuration (timeout must be 1..2000 ms)"));
        return false;
    }
    int rc = libusb_init(&m_context);
    if (rc != LIBUSB_SUCCESS) { emit errorOccurred(QString::fromLatin1(libusb_error_name(rc))); m_context = nullptr; return false; }
    m_handle = libusb_open_device_with_vid_pid(m_context, quint16(vid), quint16(pid));
    if (!m_handle) { emit errorOccurred(tr("USB device not found or access denied")); close(); return false; }
    if (libusb_kernel_driver_active(m_handle, m_interfaceNumber) == 1) {
        rc = libusb_detach_kernel_driver(m_handle, m_interfaceNumber);
        if (rc != LIBUSB_SUCCESS) { emit errorOccurred(QString::fromLatin1(libusb_error_name(rc))); close(); return false; }
        m_detached = true;
    }
    rc = libusb_claim_interface(m_handle, m_interfaceNumber);
    if (rc != LIBUSB_SUCCESS) { emit errorOccurred(QString::fromLatin1(libusb_error_name(rc))); close(); return false; }
    m_claimed = true;
    m_open = true;
    m_io.start(
        [this]() -> UsbIoWorker::ReadResult {
            QByteArray buffer(4096, '\0');
            int transferred = 0;
            const int result = libusb_bulk_transfer(m_handle, static_cast<unsigned char>(m_epIn),
                reinterpret_cast<unsigned char*>(buffer.data()), int(buffer.size()), &transferred, unsigned(std::min(50, m_timeoutMs)));
            // On timeout libusb documents transferred as valid. Other failures are not accepted as data.
            if (result == LIBUSB_SUCCESS || result == LIBUSB_ERROR_TIMEOUT) {
                buffer.resize(std::clamp(transferred, 0, int(buffer.size())));
                return {buffer, {}};
            }
            return {{}, tr("USB read failed: %1").arg(QString::fromLatin1(libusb_error_name(result)))};
        },
        [this](const QByteArray& bytes, const std::atomic_bool& running) -> QString {
            QElapsedTimer deadline;
            deadline.start();
            int offset = 0;
            while (offset < bytes.size() && running) {
                const int remaining = m_timeoutMs - int(deadline.elapsed());
                if (remaining <= 0) return tr("USB write timed out after %1 of %2 bytes; command was not retried").arg(offset).arg(bytes.size());
                int transferred = 0;
                const int rc = libusb_bulk_transfer(m_handle, static_cast<unsigned char>(m_epOut),
                    reinterpret_cast<unsigned char*>(const_cast<char*>(bytes.constData() + offset)),
                    int(bytes.size()) - offset, &transferred, unsigned(std::min(remaining, 50)));
                if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT)
                    return tr("USB write failed after %1 bytes: %2").arg(offset).arg(QString::fromLatin1(libusb_error_name(rc)));
                if (transferred < 0 || transferred > bytes.size() - offset) return tr("Invalid USB transfer length");
                offset += transferred; // Continue only the untransferred suffix, never resend a prefix.
            }
            return {};
        },
        [this](const QByteArray& bytes) { emit dataReceived(bytes); },
        [this](const QString& message) { emit errorOccurred(message); },
        [this]() { if (m_open) { m_open = false; emit statusChanged(false); } });
    emit statusChanged(true);
    return true;
#else
    Q_UNUSED(config)
    emit errorOccurred(tr("USB Raw (Linux) requires Linux and libusb-1.0"));
    return false;
#endif
}
void UsbRawLinuxPlugin::close()
{
    m_io.stop(); // All blocking reads use a finite <= 50 ms timeout.
#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    if (m_handle) {
        if (m_claimed) libusb_release_interface(m_handle, m_interfaceNumber);
        if (m_detached) libusb_attach_kernel_driver(m_handle, m_interfaceNumber);
        libusb_close(m_handle);
        m_handle = nullptr;
    }
    if (m_context) { libusb_exit(m_context); m_context = nullptr; }
#endif
    m_claimed = false;
    m_detached = false;
    if (m_open) { m_open = false; emit statusChanged(false); }
}
bool UsbRawLinuxPlugin::isOpen() const { return m_open && m_io.running(); }
qint64 UsbRawLinuxPlugin::write(const QByteArray& bytes)
{
    if (!isOpen()) return -1;
    const qint64 accepted = m_io.enqueue(bytes);
    if (accepted < 0) emit errorOccurred(tr("USB TX queue full or command exceeds 64 KiB"));
    return accepted;
}
QString UsbRawLinuxPlugin::name() const { return QStringLiteral("USB Raw (Linux)"); }
QString UsbRawLinuxPlugin::version() const { return QStringLiteral("2.0.0"); }
QVariantMap UsbRawLinuxPlugin::defaultConfig() const
{
    return {{"vid", "0x0483"}, {"pid", "0x5740"}, {"ep_out", "0x01"}, {"ep_in", "0x81"}, {"interface", 0}, {"timeout_ms", 1000}};
}
int UsbRawLinuxPlugin::parseHexInt(const QVariant& value, int fallback)
{
    bool ok = false;
    const auto text = value.toString().trimmed();
    int result = text.toInt(&ok, 0);
    if (!ok) result = text.toInt(&ok, 16);
    return ok ? result : fallback;
}
