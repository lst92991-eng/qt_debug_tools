#pragma once
#include "sdk/IPhysicalPlugin.h"
#include "sdk/UsbIoWorker.h"
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winusb.h>
#endif
class UsbRawWinPlugin : public IPhysicalPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IPhysicalPlugin_iid FILE "usb_raw_win.json")
    Q_INTERFACES(IPhysicalPlugin)
public:
    explicit UsbRawWinPlugin(QObject* parent = nullptr);
    ~UsbRawWinPlugin() override;
    bool open(const QVariantMap& config) override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data) override;
    QString name() const override;
    QString version() const override;
    QVariantMap defaultConfig() const override;
private:
    static int parseHexInt(const QVariant& value, int fallback);
    UsbIoWorker m_io;
    bool m_open = false;
    int m_epOut = 0x01;
    int m_epIn = 0x81;
    unsigned long m_timeoutMs = 1000;
#if defined(Q_OS_WIN)
    HANDLE m_device = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE m_usb = nullptr;
#endif
};
