#include "UsbRawWinPlugin.h"

#include <QRegularExpression>

#if defined(Q_OS_WIN)
#include <setupapi.h>

#include <objbase.h>

namespace {
QString lastWindowsError(const QString& prefix)
{
    const DWORD error = GetLastError();
    LPWSTR buffer = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    const QString message = buffer ? QString::fromWCharArray(buffer).trimmed() : QString::number(error);
    if (buffer) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1: %2").arg(prefix, message);
}

bool parseGuid(const QString& text, GUID* guid)
{
    if (!guid) {
        return false;
    }
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }
    return CLSIDFromString(reinterpret_cast<LPCOLESTR>(normalized.utf16()), guid) == S_OK;
}

QString findDevicePath(const GUID& guid, int vid, int pid)
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return {};
    }

    QString found;
    for (DWORD index = 0; found.isEmpty(); ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr, &guid, index, &interfaceData)) {
            break;
        }

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0) {
            continue;
        }

        QByteArray storage(requiredSize, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, detail, requiredSize, nullptr, nullptr)) {
            continue;
        }

        const QString path = QString::fromWCharArray(detail->DevicePath);
        const QString lower = path.toLower();
        const QString vidNeedle = QStringLiteral("vid_%1").arg(vid, 4, 16, QLatin1Char('0')).toLower();
        const QString pidNeedle = QStringLiteral("pid_%1").arg(pid, 4, 16, QLatin1Char('0')).toLower();
        if ((vid <= 0 || lower.contains(vidNeedle)) && (pid <= 0 || lower.contains(pidNeedle))) {
            found = path;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}
}
#endif

UsbRawWinPlugin::UsbRawWinPlugin(QObject* parent)
    : IPhysicalPlugin(parent)
{
}

bool UsbRawWinPlugin::open(const QVariantMap& config)
{
#if defined(Q_OS_WIN)
    close();

    const QVariantMap defaults = defaultConfig();
    GUID guid = {};
    if (!parseGuid(config.value(QStringLiteral("device_guid"), defaults.value(QStringLiteral("device_guid"))).toString(), &guid)) {
        emit errorOccurred(tr("USB Raw (Windows) requires a WinUSB device interface GUID"));
        return false;
    }

    const int vid = parseHexInt(config.value(QStringLiteral("vid"), defaults.value(QStringLiteral("vid"))), 0);
    const int pid = parseHexInt(config.value(QStringLiteral("pid"), defaults.value(QStringLiteral("pid"))), 0);
    m_epOut = parseHexInt(config.value(QStringLiteral("ep_out"), defaults.value(QStringLiteral("ep_out"))), 0x01);
    m_epIn = parseHexInt(config.value(QStringLiteral("ep_in"), defaults.value(QStringLiteral("ep_in"))), 0x81);
    m_timeoutMs = static_cast<unsigned long>(config.value(QStringLiteral("timeout_ms"), defaults.value(QStringLiteral("timeout_ms"))).toUInt());

    const QString devicePath = findDevicePath(guid, vid, pid);
    if (devicePath.isEmpty()) {
        emit errorOccurred(tr("No matching WinUSB device interface found"));
        return false;
    }

    m_device = CreateFileW(
        reinterpret_cast<LPCWSTR>(devicePath.utf16()),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_device == INVALID_HANDLE_VALUE) {
        emit errorOccurred(lastWindowsError(tr("CreateFile failed")));
        return false;
    }

    if (!WinUsb_Initialize(m_device, &m_usb)) {
        emit errorOccurred(lastWindowsError(tr("WinUsb_Initialize failed")));
        CloseHandle(m_device);
        m_device = INVALID_HANDLE_VALUE;
        return false;
    }

    WinUsb_SetPipePolicy(m_usb, static_cast<UCHAR>(m_epIn), PIPE_TRANSFER_TIMEOUT, sizeof(m_timeoutMs), &m_timeoutMs);
    WinUsb_SetPipePolicy(m_usb, static_cast<UCHAR>(m_epOut), PIPE_TRANSFER_TIMEOUT, sizeof(m_timeoutMs), &m_timeoutMs);

    m_running = true;
    m_open = true;
    m_worker = QThread::create([this]() { readLoop(); });
    m_worker->setObjectName(QStringLiteral("usb-raw-win-reader"));
    m_worker->start();
    emit statusChanged(true);
    return true;
#else
    Q_UNUSED(config)
    emit errorOccurred(tr("USB Raw (Windows) is only available on Windows"));
    m_open = false;
    emit statusChanged(false);
    return false;
#endif
}

void UsbRawWinPlugin::close()
{
    m_running = false;
    if (m_worker) {
        m_worker->quit();
        m_worker->wait();
        delete m_worker;
        m_worker = nullptr;
    }

#if defined(Q_OS_WIN)
    QMutexLocker locker(&m_ioMutex);
    if (m_usb) {
        WinUsb_Free(m_usb);
        m_usb = nullptr;
    }
    if (m_device != INVALID_HANDLE_VALUE) {
        CloseHandle(m_device);
        m_device = INVALID_HANDLE_VALUE;
    }
#endif

    if (m_open.exchange(false)) {
        emit statusChanged(false);
    }
}

bool UsbRawWinPlugin::isOpen() const
{
    return m_open.load();
}

qint64 UsbRawWinPlugin::write(const QByteArray& data)
{
#if defined(Q_OS_WIN)
    if (!m_open || !m_usb) {
        emit errorOccurred(tr("USB Raw (Windows) is not open"));
        return -1;
    }

    QMutexLocker locker(&m_ioMutex);
    unsigned long transferred = 0;
    if (!WinUsb_WritePipe(
            m_usb,
            static_cast<UCHAR>(m_epOut),
            reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())),
            static_cast<ULONG>(data.size()),
            &transferred,
            nullptr)) {
        emit errorOccurred(lastWindowsError(tr("WinUSB write failed")));
        return -1;
    }
    return static_cast<qint64>(transferred);
#else
    Q_UNUSED(data)
    emit errorOccurred(tr("USB Raw (Windows) is only available on Windows"));
    return -1;
#endif
}

QString UsbRawWinPlugin::name() const
{
    return QStringLiteral("USB Raw (Windows)");
}

QString UsbRawWinPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVariantMap UsbRawWinPlugin::defaultConfig() const
{
    return {
        {QStringLiteral("device_guid"), QStringLiteral("")},
        {QStringLiteral("vid"), QStringLiteral("0x0483")},
        {QStringLiteral("pid"), QStringLiteral("0x5740")},
        {QStringLiteral("ep_out"), QStringLiteral("0x01")},
        {QStringLiteral("ep_in"), QStringLiteral("0x81")},
        {QStringLiteral("timeout_ms"), 1000}
    };
}

int UsbRawWinPlugin::parseHexInt(const QVariant& value, int fallback)
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

void UsbRawWinPlugin::readLoop()
{
#if defined(Q_OS_WIN)
    QByteArray buffer(4096, '\0');
    while (m_running) {
        unsigned long transferred = 0;
        bool ok = false;
        {
            QMutexLocker locker(&m_ioMutex);
            if (!m_usb) {
                break;
            }
            ok = WinUsb_ReadPipe(
                m_usb,
                static_cast<UCHAR>(m_epIn),
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(buffer.size()),
                &transferred,
                nullptr);
        }

        if (ok && transferred > 0) {
            emit dataReceived(buffer.left(static_cast<qsizetype>(transferred)));
            continue;
        }

        const DWORD error = GetLastError();
        if (error == ERROR_SEM_TIMEOUT || error == ERROR_IO_PENDING || error == ERROR_OPERATION_ABORTED) {
            continue;
        }

        emit errorOccurred(lastWindowsError(tr("WinUSB read failed")));
        break;
    }

    if (m_open.exchange(false)) {
        emit statusChanged(false);
    }
#endif
}
