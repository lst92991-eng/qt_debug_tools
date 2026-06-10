#include "CanFramePlugin.h"

#include <QRegularExpression>

CanFramePlugin::CanFramePlugin(QObject* parent)
    : IProtocolPlugin(parent)
{
}

void CanFramePlugin::feedBytes(const QByteArray& raw)
{
    m_buffer.append(raw);
    parseBuffer();
}

QByteArray CanFramePlugin::encodeCommand(const QVariantMap& command)
{
    if (command.contains(QStringLiteral("bytes"))) {
        return command.value(QStringLiteral("bytes")).toByteArray();
    }

    const quint32 canId = command.value(QStringLiteral("can_id"), 0).toUInt();
    QByteArray payload;
    if (command.contains(QStringLiteral("payload"))) {
        payload = command.value(QStringLiteral("payload")).toByteArray();
    } else {
        payload = parseHexString(command.value(QStringLiteral("data")).toString());
    }

    if (payload.size() > 64) {
        payload.truncate(64);
    }

    QByteArray frame;
    frame.append(static_cast<char>(0xCA));
    frame.append(static_cast<char>(0xFD));
    frame.append(static_cast<char>((canId >> 24) & 0xff));
    frame.append(static_cast<char>((canId >> 16) & 0xff));
    frame.append(static_cast<char>((canId >> 8) & 0xff));
    frame.append(static_cast<char>(canId & 0xff));
    frame.append(static_cast<char>(payload.size()));
    frame.append(payload);
    return frame;
}

QString CanFramePlugin::name() const
{
    return QStringLiteral("CAN Frame");
}

QString CanFramePlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QByteArray CanFramePlugin::parseHexString(const QString& text)
{
    QString compact = text;
    compact.remove(QRegularExpression(QStringLiteral("[\\s,;:_-]")));
    if (compact.size() % 2 != 0) {
        return {};
    }

    QByteArray bytes;
    bytes.reserve(compact.size() / 2);
    for (int i = 0; i < compact.size(); i += 2) {
        bool ok = false;
        const int value = compact.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            return {};
        }
        bytes.append(static_cast<char>(value));
    }
    return bytes;
}

void CanFramePlugin::parseBuffer()
{
    while (m_buffer.size() >= 7) {
        const int start = m_buffer.indexOf(QByteArray::fromHex("cafd"));
        if (start < 0) {
            m_buffer.clear();
            return;
        }
        if (start > 0) {
            m_buffer.remove(0, start);
        }
        if (m_buffer.size() < 7) {
            return;
        }

        const quint32 canId =
            (static_cast<quint8>(m_buffer.at(2)) << 24)
            | (static_cast<quint8>(m_buffer.at(3)) << 16)
            | (static_cast<quint8>(m_buffer.at(4)) << 8)
            | static_cast<quint8>(m_buffer.at(5));
        const int dlc = static_cast<quint8>(m_buffer.at(6));
        if (dlc > 64) {
            m_buffer.remove(0, 2);
            continue;
        }
        if (m_buffer.size() < 7 + dlc) {
            return;
        }

        const QByteArray payload = m_buffer.mid(7, dlc);
        m_buffer.remove(0, 7 + dlc);

        DataFrame frame;
        frame.timestamp_us = currentTimestampMicros();
        frame.rawPayload = payload;
        frame.direction = FrameDirection::Receive;
        frame.attributes.insert(QStringLiteral("can_id"), static_cast<qulonglong>(canId));
        frame.attributes.insert(QStringLiteral("dlc"), dlc);
        frame.attributes.insert(QStringLiteral("is_fd"), dlc > 8);

        for (int i = 0; i < payload.size(); ++i) {
            ChannelSample sample;
            sample.index = static_cast<quint16>((canId & 0x7ff) * 64 + i);
            sample.value = static_cast<quint8>(payload.at(i));
            sample.name = QStringLiteral("CAN%1[%2]").arg(canId, 0, 16).arg(i).toUpper();
            sample.unit = QStringLiteral("byte");
            frame.channels.push_back(sample);
        }
        if (frame.channels.isEmpty()) {
            frame.channels = {{static_cast<quint16>(canId & 0xffff), std::numeric_limits<double>::quiet_NaN()}};
        }
        emit frameParsed(frame);
    }
}
