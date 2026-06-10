#pragma once

#include "sdk/IPhysicalPlugin.h"

#include <QSerialPort>

class SerialGenericPlugin : public IPhysicalPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IPhysicalPlugin_iid FILE "serial_generic.json")
    Q_INTERFACES(IPhysicalPlugin)

public:
    explicit SerialGenericPlugin(QObject* parent = nullptr);
    ~SerialGenericPlugin() override;

    bool open(const QVariantMap& config) override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data) override;

    QString name() const override;
    QString version() const override;
    QVariantMap defaultConfig() const override;

private:
    QSerialPort::Parity parseParity(const QString& value) const;
    QSerialPort::StopBits parseStopBits(int value) const;
    QSerialPort::DataBits parseDataBits(int value) const;
    QSerialPort::FlowControl parseFlowControl(const QString& value) const;

    QSerialPort m_port;
};
