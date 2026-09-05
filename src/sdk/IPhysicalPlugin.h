#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>
#define IPhysicalPlugin_iid "org.mcd.sdk.IPhysicalPlugin/2.0"
class IPhysicalPlugin : public QObject {
    Q_OBJECT
public:
    explicit IPhysicalPlugin(QObject* parent = nullptr) : QObject(parent) {}
    ~IPhysicalPlugin() override = default;
    virtual bool open(const QVariantMap& config) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Non-blocking acceptance into a bounded TX queue, not an on-wire acknowledgement.
    // Return the accepted byte count, or -1 without enqueueing any bytes.
    virtual qint64 write(const QByteArray& data) = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QVariantMap defaultConfig() const = 0;
signals:
    void dataReceived(const QByteArray& rawBytes);
    void errorOccurred(const QString& message);
    void statusChanged(bool connected);
};
Q_DECLARE_INTERFACE(IPhysicalPlugin, IPhysicalPlugin_iid)
