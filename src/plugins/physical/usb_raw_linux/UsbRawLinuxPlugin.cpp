#include "UsbRawLinuxPlugin.h"

#include <QRegularExpression>

UsbRawLinuxPlugin::UsbRawLinuxPlugin(QObject* parent)
    : IPhysicalPlugin(parent)
{
}

UsbRawLinuxPlugin::~UsbRawLinuxPlugin()
{
    close();
}

bool UsbRawLinuxPlugin::open(const QVariantMap& config)
{
    close();

#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    const QVariantMap defaults = defaultConfig();
    const int vid = parseHexInt(config.value(QStringLiteral("vid"), defaults.value(QStringLiteral("vid"))), 0);
    const int pid = parseHexInt(config.value(QStringLiteral("pid"), defaults.value(QStringLiteral("pid"))), 0);
    m_epOut = parseHexInt(config.value(QStringLiteral("ep_out"), defaults.value(QStringLiteral("ep_out"))), 0x01);
    m_epIn = parseHexInt(config.value(QStringLiteral("ep_in"), defaults.value(QStringLiteral("ep_in"))), 0x81);
    m_timeoutMs = config.value(QStringLiteral("timeout_ms"), defaults.value(QStringLiteral("timeout_ms"))).toInt();
    m_interfaceNumber = config.value(QStringLiteral("interface"), defaults.value(QStringLiteral("interface"))).toInt();

    if (vid <= 0 || pid <= 0) {
        emit errorOccurred(tr("USB VID/PID must be configured"));
        return false;
    }

    int rc = libusb_init(&m_context);
    if (rc != LIBUSB_SUCCESS) {
        emit errorOccurred(tr("libusb_init failed: %1").arg(libusb_error_name(rc)));
        return false;
    }

    m_handle = libusb_open_device_with_vid_pid(m_context, vid, pid);
    if (!m_handle) {
        emit errorOccurred(tr("USB device not found: VID 0x%1 PID 0x%2")
                               .arg(vid, 4, 16, QLatin1Char('0'))
                               .arg(pid, 4, 16, QLatin1Char('0')));
        libusb_exit(m_context);
        m_context = nullptr;
        return false;
    }

    if (libusb_kernel_driver_active(m_handle, m_interfaceNumber) == 1) {
        libusb_detach_kernel_driver(m_handle, m_interfaceNumber);
    }

    rc = libusb_claim_interface(m_handle, m_interfaceNumber);
    if (rc != LIBUSB_SUCCESS) {
        emit errorOccurred(tr("Failed to claim USB interface: %1").arg(libusb_error_name(rc)));
        libusb_close(m_handle);
        libusb_exit(m_context);
        m_handle = nullptr;
        m_context = nullptr;
        return false;
    }

    m_running = true;
    m_open = true;
    m_worker = QThread::create([this]() { readLoop(); });
    m_worker->setObjectName(QStringLiteral("usb-raw-linux-reader"));
    m_worker->start();
    emit statusChanged(true);
    return true;
#else
    Q_UNUSED(config)
    emit errorOccurred(tr("USB Raw (Linux) requires Linux with libusb-1.0 available at build time"));
    emit statusChanged(false);
    return false;
#endif
}

void UsbRawLinuxPlugin::close()
{
    m_running = false;
    if (m_worker) {
        m_worker->quit();
        m_worker->wait();
        delete m_worker;
        m_worker = nullptr;
    }

#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    QMutexLocker locker(&m_ioMutex);
    if (m_handle) {
        libusb_release_interface(m_handle, m_interfaceNumber);
        libusb_close(m_handle);
        m_handle = nullptr;
    }
    if (m_context) {
        libusb_exit(m_context);
        m_context = nullptr;
    }
#endif

    if (m_open) {
        m_open = false;
        emit statusChanged(false);
    }
}

bool UsbRawLinuxPlugin::isOpen() const
{
    return m_open.load();
}

qint64 UsbRawLinuxPlugin::write(const QByteArray& data)
{
#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    if (!m_open || !m_handle) {
        emit errorOccurred(tr("USB device is not open"));
        return -1;
    }

    QMutexLocker locker(&m_ioMutex);
    int transferred = 0;
    const int rc = libusb_bulk_transfer(
        m_handle,
        static_cast<unsigned char>(m_epOut),
        const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data.constData())),
        data.size(),
        &transferred,
        m_timeoutMs);

    if (rc != LIBUSB_SUCCESS) {
        emit errorOccurred(tr("USB write failed: %1").arg(libusb_error_name(rc)));
        return -1;
    }
    return transferred;
#else
    Q_UNUSED(data)
    emit errorOccurred(tr("USB Raw (Linux) is not available in this build"));
    return -1;
#endif
}

QString UsbRawLinuxPlugin::name() const
{
    return QStringLiteral("USB Raw (Linux)");
}

QString UsbRawLinuxPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVariantMap UsbRawLinuxPlugin::defaultConfig() const
{
    return {
        {QStringLiteral("vid"), QStringLiteral("0x0483")},
        {QStringLiteral("pid"), QStringLiteral("0x5740")},
        {QStringLiteral("ep_out"), QStringLiteral("0x01")},
        {QStringLiteral("ep_in"), QStringLiteral("0x81")},
        {QStringLiteral("interface"), 0},
        {QStringLiteral("timeout_ms"), 1000}
    };
}

int UsbRawLinuxPlugin::parseHexInt(const QVariant& value, int fallback)
{
    bool ok = false;
    const int direct = value.toInt(&ok);
    if (ok) {
        return direct;
    }

    QString text = value.toString().trimmed();
    int parsed = text.toInt(&ok, 0);
    if (!ok) {
        text.remove(QRegularExpression(QStringLiteral("[\\s,;:_-]")));
        parsed = text.toInt(&ok, 16);
    }
    return ok ? parsed : fallback;
}

void UsbRawLinuxPlugin::readLoop()
{
#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    QByteArray buffer(4096, '\0');
    while (m_running) {
        int transferred = 0;
        int rc = LIBUSB_ERROR_NO_DEVICE;
        {
            QMutexLocker locker(&m_ioMutex);
            if (!m_handle) {
                break;
            }
            rc = libusb_bulk_transfer(
                m_handle,
                static_cast<unsigned char>(m_epIn),
                reinterpret_cast<unsigned char*>(buffer.data()),
                buffer.size(),
                &transferred,
                m_timeoutMs);
        }

        if (rc == LIBUSB_SUCCESS && transferred > 0) {
            emit dataReceived(buffer.left(transferred));
            continue;
        }

        if (rc == LIBUSB_ERROR_TIMEOUT) {
            continue;
        }

        emit errorOccurred(tr("USB read failed: %1").arg(libusb_error_name(rc)));
        break;
    }

    m_open = false;
    emit statusChanged(false);
#endif
}
