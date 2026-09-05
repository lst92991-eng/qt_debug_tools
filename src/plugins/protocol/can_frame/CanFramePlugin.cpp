#include "CanFramePlugin.h"
#include <QRegularExpression>
#include <algorithm>
#include <limits>
CanFramePlugin::CanFramePlugin(QObject* parent) : IProtocolPlugin(parent) {}
void CanFramePlugin::feedBytes(const QByteArray& raw)
{
    // Bound the parser working set even when a caller provides a very large burst.
    for (qsizetype offset = 0; offset < raw.size(); offset += 4096) {
        m_buffer.append(raw.constData() + offset, std::min<qsizetype>(4096, raw.size() - offset));
        parseBuffer();
    }
}
QByteArray CanFramePlugin::encodeCommand(const QVariantMap& command)
{
    // The SL slider format is not a CAN frame. Raw Control remains an explicit byte-level sender.
    if (command.value("source").toString() == "slider_widget") return {};
    if (command.contains("bytes")) return command.value("bytes").toByteArray();
    bool ok = false;
    const quint64 id = command.value("can_id", 0).toULongLong(&ok);
    if (!ok || id > std::numeric_limits<quint32>::max()) return {};
    QByteArray payload;
    if (command.contains("payload")) payload = command.value("payload").toByteArray();
    else {
        QString text = command.value("data").toString();
        text.remove(QRegularExpression(QStringLiteral("[\\s,;:_-]")));
        static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]*$"));
        if (text.size() % 2 || !hex.match(text).hasMatch()) return {};
        payload = QByteArray::fromHex(text.toLatin1());
    }
    if (payload.size() > 64) return {}; // Never silently truncate a hardware command.
    QByteArray bytes = QByteArray::fromHex("cafd");
    for (int shift = 24; shift >= 0; shift -= 8) bytes.append(char((id >> shift) & 0xff));
    bytes.append(char(payload.size()));
    bytes.append(payload);
    return bytes;
}
void CanFramePlugin::reset() { m_buffer.clear(); }
QString CanFramePlugin::name() const { return QStringLiteral("CAN Frame"); }
QString CanFramePlugin::version() const { return QStringLiteral("2.0.0"); }
QByteArray CanFramePlugin::parseHexString(const QString& text)
{
    QString compact = text;
    compact.remove(QRegularExpression(QStringLiteral("[\\s,;:_-]")));
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]*$"));
    if (compact.size() % 2 || !hex.match(compact).hasMatch()) return {};
    return QByteArray::fromHex(compact.toLatin1());
}
void CanFramePlugin::parseBuffer()
{
    static const QByteArray header = QByteArray::fromHex("cafd");
    while (m_buffer.size() >= 2) {
        const qsizetype start = m_buffer.indexOf(header);
        if (start < 0) {
            m_buffer = m_buffer.endsWith(char(0xca)) ? QByteArray(1, char(0xca)) : QByteArray{};
            return;
        }
        if (start > 0) m_buffer.remove(0, start);
        if (m_buffer.size() < 7) return;
        const quint32 id = (quint32(quint8(m_buffer.at(2))) << 24)
            | (quint32(quint8(m_buffer.at(3))) << 16) | (quint32(quint8(m_buffer.at(4))) << 8)
            | quint32(quint8(m_buffer.at(5)));
        const int length = quint8(m_buffer.at(6));
        if (length > 64) { m_buffer.remove(0, 1); continue; }
        if (m_buffer.size() < 7 + length) return;
        DataFrame frame;
        frame.timestamp_us = currentTimestampMicros();
        frame.rawPayload = m_buffer.left(7 + length); // Preserve the complete transport envelope.
        const auto payload = m_buffer.mid(7, length);
        m_buffer.remove(0, 7 + length);
        frame.attributes.insert("can_id", qulonglong(id));
        frame.attributes.insert("payload_length", length);
        frame.attributes.insert("dlc", length); // Legacy key: byte count, NOT encoded CAN-FD DLC.
        frame.attributes.insert("payload", payload);
        // This legacy envelope has no FD/IDE/BRS flags; do not invent them from a short length.
        if (length > 8) frame.attributes.insert("requires_fd", true);
        const ChannelId base = ChannelId(id) << 6;
        for (int i = 0; i < length; ++i) {
            frame.channels.push_back({base | ChannelId(i), double(quint8(payload.at(i))),
                QStringLiteral("CAN%1[%2]").arg(id, 0, 16).arg(i).toUpper(), QStringLiteral("byte")});
        }
        emit frameParsed(frame);
    }
}
