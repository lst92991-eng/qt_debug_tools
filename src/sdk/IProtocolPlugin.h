#pragma once
#include "sdk/DataFrame.h"
#include <QObject>
#include <QString>
#include <QVariantMap>

// ChannelId and the virtual interface changed: old plugin binaries must be rebuilt.
#define IProtocolPlugin_iid "org.mcd.sdk.IProtocolPlugin/2.0"
class IProtocolPlugin : public QObject {
    Q_OBJECT
public:
    explicit IProtocolPlugin(QObject* parent = nullptr) : QObject(parent) {}
    ~IProtocolPlugin() override = default;
    virtual void feedBytes(const QByteArray& raw) = 0;
    virtual QByteArray encodeCommand(const QVariantMap& command) = 0;
    virtual void reset() {}
    virtual QString name() const = 0;
    virtual QString version() const = 0;
signals:
    void frameParsed(const DataFrame& frame);
};
Q_DECLARE_INTERFACE(IProtocolPlugin, IProtocolPlugin_iid)
