#pragma once

#include "sdk/IProtocolPlugin.h"

class RawPassthroughPlugin : public IProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IProtocolPlugin_iid FILE "raw_passthrough.json")
    Q_INTERFACES(IProtocolPlugin)

public:
    explicit RawPassthroughPlugin(QObject* parent = nullptr);

    void feedBytes(const QByteArray& raw) override;
    QByteArray encodeCommand(const QVariantMap& command) override;
    QString name() const override;
    QString version() const override;

private:
    static QByteArray parseHexString(const QString& text);
};
