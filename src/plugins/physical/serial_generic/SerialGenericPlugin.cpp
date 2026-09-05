#include "SerialGenericPlugin.h"
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QStringList>
SerialGenericPlugin::SerialGenericPlugin(QObject* parent) : IPhysicalPlugin(parent)
{
    m_port.setReadBufferSize(64 * 1024);
    connect(&m_port, &QSerialPort::readyRead, this, [this]() {
        const auto bytes = m_port.readAll();
        if (!bytes.isEmpty()) emit dataReceived(bytes);
    });
    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) return;
        emit errorOccurred(m_port.errorString());
        if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError
            || error == QSerialPort::PermissionError || error == QSerialPort::ReadError || error == QSerialPort::WriteError) {
            const quint64 generation = m_generation;
            QMetaObject::invokeMethod(this, [this, generation]() { if (generation == m_generation) close(); }, Qt::QueuedConnection);
        }
    });
}
SerialGenericPlugin::~SerialGenericPlugin() { close(); }
bool SerialGenericPlugin::open(const QVariantMap& config)
{
    close();
    const auto defaults = defaultConfig();
    const auto value = [&](const QString& key) { return config.value(key, defaults.value(key)); };
    const auto port = value("port").toString().trimmed();
    bool baudOk = false, dataOk = false, stopOk = false;
    const int baud = value("baud").toInt(&baudOk);
    const int bits = value("data_bits").toInt(&dataOk);
    const int stopBits = value("stop_bits").toInt(&stopOk);
    const auto parity = value("parity").toString().trimmed().toLower();
    const auto flow = value("flow_control").toString().trimmed().toLower();
    if (port.isEmpty() || !baudOk || baud <= 0 || !dataOk || bits < 5 || bits > 8
        || !stopOk || (stopBits != 1 && stopBits != 2)
        || !QStringList{"none", "even", "odd", "mark", "space"}.contains(parity)
        || !QStringList{"none", "hardware", "software"}.contains(flow)) {
        emit errorOccurred(tr("Invalid serial configuration")); emit statusChanged(false); return false;
    }
    m_port.setPortName(port);
    if (!m_port.setBaudRate(baud) || !m_port.setDataBits(parseDataBits(bits))
        || !m_port.setStopBits(parseStopBits(stopBits)) || !m_port.setParity(parseParity(parity))
        || !m_port.setFlowControl(parseFlowControl(flow)) || !m_port.open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_port.errorString()); emit statusChanged(false); return false;
    }
    emit statusChanged(true);
    return true;
}
void SerialGenericPlugin::close()
{
    ++m_generation;
    if (m_port.isOpen()) {
        const QSignalBlocker blocker(&m_port);
        m_port.close();
        emit statusChanged(false);
    }
}
bool SerialGenericPlugin::isOpen() const { return m_port.isOpen(); }
qint64 SerialGenericPlugin::write(const QByteArray& bytes)
{
    if (!isOpen()) return -1;
    if (bytes.size() > 64 * 1024 || m_port.bytesToWrite() + bytes.size() > 1024 * 1024) {
        emit errorOccurred(tr("Serial TX queue full or command exceeds 64 KiB")); return -1;
    }
    return m_port.write(bytes);
}
QString SerialGenericPlugin::name() const { return QStringLiteral("Serial (Generic)"); }
QString SerialGenericPlugin::version() const { return QStringLiteral("2.0.0"); }
QVariantMap SerialGenericPlugin::defaultConfig() const
{
    const auto ports = QSerialPortInfo::availablePorts();
    const QString port = ports.isEmpty() ? QString{} : ports.first().portName();
    return {{"port", port}, {"baud", 115200}, {"data_bits", 8}, {"stop_bits", 1}, {"parity", "none"}, {"flow_control", "none"}};
}
QSerialPort::Parity SerialGenericPlugin::parseParity(const QString& value) const
{
    if (value == "even") return QSerialPort::EvenParity;
    if (value == "odd") return QSerialPort::OddParity;
    if (value == "mark") return QSerialPort::MarkParity;
    if (value == "space") return QSerialPort::SpaceParity;
    return QSerialPort::NoParity;
}
QSerialPort::StopBits SerialGenericPlugin::parseStopBits(int value) const { return value == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop; }
QSerialPort::DataBits SerialGenericPlugin::parseDataBits(int value) const { return static_cast<QSerialPort::DataBits>(value); }
QSerialPort::FlowControl SerialGenericPlugin::parseFlowControl(const QString& value) const
{
    if (value == "hardware") return QSerialPort::HardwareControl;
    if (value == "software") return QSerialPort::SoftwareControl;
    return QSerialPort::NoFlowControl;
}
