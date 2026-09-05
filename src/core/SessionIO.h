#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QString>
namespace SessionIO {
inline constexpr qint64 MaxBytes = 1024 * 1024;
inline bool parse(const QByteArray& bytes, QJsonObject* result, QString* error)
{
    const auto fail = [error](const QString& message) { if (error) *error = message; return false; };
    if (!result || bytes.size() > MaxBytes) return fail(QStringLiteral("Session exceeds size limit"));
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("Invalid session JSON: %1").arg(parseError.errorString()));
    const auto root = doc.object();
    if (root.contains("version") && (!root.value("version").isDouble()
        || (root.value("version").toDouble() != 1 && root.value("version").toDouble() != 2)))
        return fail(QStringLiteral("Unsupported session version"));
    for (const QString& key : {QStringLiteral("physical"), QStringLiteral("protocol")}) {
        if (!root.value(key).isString() || root.value(key).toString().size() > 256)
            return fail(QStringLiteral("Invalid plugin selection"));
    }
    if (!root.value("physical_configs").isObject() || !root.value("channel_metadata").isObject())
        return fail(QStringLiteral("Missing session configuration objects"));
    const auto configs = root.value("physical_configs").toObject();
    if (configs.size() > 64) return fail(QStringLiteral("Too many device configurations"));
    for (auto it = configs.begin(); it != configs.end(); ++it) {
        if (!it.value().isObject() || it.key().size() > 256) return fail(QStringLiteral("Invalid device configuration"));
        const auto config = it.value().toObject();
        if (config.size() > 64) return fail(QStringLiteral("Too many device configuration fields"));
        for (auto field = config.begin(); field != config.end(); ++field) {
            if (field.key().size() > 128 || field.value().isArray() || field.value().isObject()
                || field.value().isNull() || (field.value().isString() && field.value().toString().size() > 4096))
                return fail(QStringLiteral("Invalid device configuration value"));
        }
    }
    const auto metadata = root.value("channel_metadata").toObject();
    if (metadata.size() > 1024) return fail(QStringLiteral("Too many channel metadata entries"));
    static const QRegularExpression channelPattern(QStringLiteral("^(0|[1-9][0-9]{0,19})$"));
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        bool ok = false;
        it.key().toULongLong(&ok);
        if (!ok || !channelPattern.match(it.key()).hasMatch() || !it.value().isObject())
            return fail(QStringLiteral("Invalid channel identifier"));
        const auto meta = it.value().toObject();
        for (const QString& key : {QStringLiteral("name"), QStringLiteral("unit")})
            if (!meta.value(key).isString() || meta.value(key).toString().size() > 256)
                return fail(QStringLiteral("Invalid channel name or unit"));
    }
    *result = root;
    if (error) error->clear();
    return true;
}
}
