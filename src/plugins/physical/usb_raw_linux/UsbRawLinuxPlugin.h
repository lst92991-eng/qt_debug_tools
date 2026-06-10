#pragma once

#include "sdk/IPhysicalPlugin.h"

#include <QMutex>
#include <QThread>

#include <atomic>

#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
#include <libusb-1.0/libusb.h>
#endif

class UsbRawLinuxPlugin : public IPhysicalPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IPhysicalPlugin_iid FILE "usb_raw_linux.json")
    Q_INTERFACES(IPhysicalPlugin)

public:
    explicit UsbRawLinuxPlugin(QObject* parent = nullptr);
    ~UsbRawLinuxPlugin() override;

    bool open(const QVariantMap& config) override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data) override;

    QString name() const override;
    QString version() const override;
    QVariantMap defaultConfig() const override;

private:
    static int parseHexInt(const QVariant& value, int fallback);
    void readLoop();

    std::atomic_bool m_running = false;
    QThread* m_worker = nullptr;
    mutable QMutex m_ioMutex;
    std::atomic_bool m_open = false;
    int m_epOut = 0x01;
    int m_epIn = 0x81;
    int m_timeoutMs = 1000;
    int m_interfaceNumber = 0;

#if defined(Q_OS_LINUX) && defined(MCD_HAVE_LIBUSB)
    libusb_context* m_context = nullptr;
    libusb_device_handle* m_handle = nullptr;
#endif
};
