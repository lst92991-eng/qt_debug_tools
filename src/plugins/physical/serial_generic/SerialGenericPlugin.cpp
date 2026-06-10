#include "SerialGenericPlugin.h"

#include <QSerialPortInfo>

SerialGenericPlugin::SerialGenericPlugin(QObject* parent)
    : IPhysicalPlugin(parent)
{
    connect(&m_port, &QSerialPort::readyRead, this, [this]() {
        const QByteArray data = m_port.readAll();
        if (!data.isEmpty()) {
            emit dataReceived(data);
        }
    });
    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error != QSerialPort::NoError) {
            emit errorOccurred(m_port.errorString());
        }
    });
}

SerialGenericPlugin::~SerialGenericPlugin()
{
    close();
}

bool SerialGenericPlugin::open(const QVariantMap& config)
{
    close();

    const QVariantMap defaults = defaultConfig();
    const QString portName = config.value(QStringLiteral("port"), defaults.value(QStringLiteral("port"))).toString();
    if (portName.isEmpty()) {
        emit errorOccurred(tr("Serial port is empty"));
        return false;
    }

    m_port.setPortName(portName);
    m_port.setBaudRate(config.value(QStringLiteral("baud"), defaults.value(QStringLiteral("baud"))).toInt());
    m_port.setDataBits(parseDataBits(config.value(QStringLiteral("data_bits"), 8).toInt()));
    m_port.setStopBits(parseStopBits(config.value(QStringLiteral("stop_bits"), 1).toInt()));
    m_port.setParity(parseParity(config.value(QStringLiteral("parity"), QStringLiteral("none")).toString()));
    m_port.setFlowControl(parseFlowControl(config.value(QStringLiteral("flow_control"), QStringLiteral("none")).toString()));

    if (!m_port.open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_port.errorString());
        emit statusChanged(false);
        return false;
    }

    emit statusChanged(true);
    return true;
}

void SerialGenericPlugin::close()
{
    if (m_port.isOpen()) {
        m_port.close();
        emit statusChanged(false);
    }
}

bool SerialGenericPlugin::isOpen() const
{
    return m_port.isOpen();
}

qint64 SerialGenericPlugin::write(const QByteArray& data)
{
    if (!m_port.isOpen()) {
        emit errorOccurred(tr("Serial port is not open"));
        return -1;
    }
    return m_port.write(data);
}

QString SerialGenericPlugin::name() const
{
    return QStringLiteral("Serial (Generic)");
}

QString SerialGenericPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVariantMap SerialGenericPlugin::defaultConfig() const
{
    QString defaultPort;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (!ports.isEmpty()) {
        defaultPort = ports.first().portName();
    }

    return {
        {QStringLiteral("port"), defaultPort},
        {QStringLiteral("baud"), 115200},
        {QStringLiteral("data_bits"), 8},
        {QStringLiteral("stop_bits"), 1},
        {QStringLiteral("parity"), QStringLiteral("none")},
        {QStringLiteral("flow_control"), QStringLiteral("none")}
    };
}

QSerialPort::Parity SerialGenericPlugin::parseParity(const QString& value) const
{
    const QString v = value.toLower();
    if (v == QStringLiteral("even")) {
        return QSerialPort::EvenParity;
    }
    if (v == QStringLiteral("odd")) {
        return QSerialPort::OddParity;
    }
    if (v == QStringLiteral("mark")) {
        return QSerialPort::MarkParity;
    }
    if (v == QStringLiteral("space")) {
        return QSerialPort::SpaceParity;
    }
    return QSerialPort::NoParity;
}

QSerialPort::StopBits SerialGenericPlugin::parseStopBits(int value) const
{
    return value == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop;
}

QSerialPort::DataBits SerialGenericPlugin::parseDataBits(int value) const
{
    switch (value) {
    case 5:
        return QSerialPort::Data5;
    case 6:
        return QSerialPort::Data6;
    case 7:
        return QSerialPort::Data7;
    default:
        return QSerialPort::Data8;
    }
}

QSerialPort::FlowControl SerialGenericPlugin::parseFlowControl(const QString& value) const
{
    const QString v = value.toLower();
    if (v == QStringLiteral("hardware")) {
        return QSerialPort::HardwareControl;
    }
    if (v == QStringLiteral("software")) {
        return QSerialPort::SoftwareControl;
    }
    return QSerialPort::NoFlowControl;
}
