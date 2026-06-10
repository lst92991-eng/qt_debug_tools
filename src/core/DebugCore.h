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

    PluginManager* pluginManager();
    ChannelHub* channelHub();
    RingBufferPool* ringBufferPool();

    void initialize();
    void publish(const DataFrame& frame);
    void sendCommand(const QVariantMap& command);

signals:
    void framePublished(const DataFrame& frame);
    void errorOccurred(const QString& message);
    void commandSent(const QByteArray& bytes);

private:
    explicit DebugCore(QObject* parent = nullptr);
    void wireDataPath();

    PluginManager m_pluginMgr;
    ChannelHub m_channelHub;
    RingBufferPool m_ringPool;
    QMetaObject::Connection m_physicalDataConnection;
    QMetaObject::Connection m_physicalErrorConnection;
    QMetaObject::Connection m_protocolFrameConnection;
    bool m_initialized = false;
};
