#include "RawPassthroughPlugin.h"

#include <QRegularExpression>

RawPassthroughPlugin::RawPassthroughPlugin(QObject* parent)
    : IProtocolPlugin(parent)
{
}

void RawPassthroughPlugin::feedBytes(const QByteArray& raw)
{
    if (raw.isEmpty()) {
        return;
    }

    DataFrame frame;
    frame.timestamp_us = currentTimestampMicros();
    frame.channels = {{0, std::numeric_limits<double>::quiet_NaN()}};
    frame.rawPayload = raw;
    frame.direction = FrameDirection::Receive;
    emit frameParsed(frame);
}

QByteArray RawPassthroughPlugin::encodeCommand(const QVariantMap& command)
{
    if (command.contains(QStringLiteral("bytes"))) {
        return command.value(QStringLiteral("bytes")).toByteArray();
    }
    return parseHexString(command.value(QStringLiteral("hex")).toString());
}

QString RawPassthroughPlugin::name() const
{
    return QStringLiteral("Raw Protocol");
}

QString RawPassthroughPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QByteArray RawPassthroughPlugin::parseHexString(const QString& text)
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
