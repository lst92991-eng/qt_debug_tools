#pragma once
#include "core/ChannelHub.h"
#include "core/PluginManager.h"
#include "core/RingBufferPool.h"
#include "sdk/DataFrame.h"
#include <QObject>
#include <QVariantMap>
class DebugCore : public QObject {
    Q_OBJECT
public:
    static DebugCore* instance();
    ~DebugCore() override;
    PluginManager* pluginManager();
    ChannelHub* channelHub();
    RingBufferPool* ringBufferPool();
    void initialize();
    void publish(const DataFrame& frame);
    void sendCommand(const QVariantMap& command);
    QVariantMap channelMetadata() const;
    void setChannelMetadata(ChannelId channel, const QString& name, const QString& unit);
    void setChannelMetadata(const QVariantMap& metadata);
    void clearHistory();
    void replayHistory();
signals:
    void framePublished(const DataFrame& frame);
    void errorOccurred(const QString& message);
    void commandSent(const QByteArray& bytes); // accepted into the TX queue
    void connectionChanged(bool connected);
private:
    explicit DebugCore(QObject* parent = nullptr);
    void wireDataPath();
    void invalidateDataPath();
    void enrich(DataFrame& frame);
    PluginManager m_pluginMgr;
    ChannelHub m_channelHub;
    RingBufferPool m_ringPool;
    QVariantMap m_channelMetadata;
    QVariantMap m_channelOverrides;
    QMetaObject::Connection m_physicalDataConnection;
    QMetaObject::Connection m_physicalErrorConnection;
    QMetaObject::Connection m_physicalStatusConnection;
    QMetaObject::Connection m_protocolFrameConnection;
    quint64 m_epoch = 0;
    bool m_initialized = false;
    bool m_historyLimitReported = false;
};
