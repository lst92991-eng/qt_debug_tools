#pragma once

#include "sdk/IProtocolPlugin.h"

class CanFramePlugin : public IProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IProtocolPlugin_iid FILE "can_frame.json")
    Q_INTERFACES(IProtocolPlugin)

public:
    explicit CanFramePlugin(QObject* parent = nullptr);

    void feedBytes(const QByteArray& raw) override;
    QByteArray encodeCommand(const QVariantMap& command) override;
    QString name() const override;
    QString version() const override;

private:
    static QByteArray parseHexString(const QString& text);
    void parseBuffer();

    QByteArray m_buffer;
};
