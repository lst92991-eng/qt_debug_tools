#include "core/DebugCore.h"
#include <QPointer>
#include <cmath>
#include <limits>
DebugCore* DebugCore::instance() { static DebugCore core; return &core; }
DebugCore::DebugCore(QObject* parent) : QObject(parent), m_pluginMgr(this), m_channelHub(this) {}
DebugCore::~DebugCore() { invalidateDataPath(); m_pluginMgr.clear(); }
PluginManager* DebugCore::pluginManager() { return &m_pluginMgr; }
ChannelHub* DebugCore::channelHub() { return &m_channelHub; }
RingBufferPool* DebugCore::ringBufferPool() { return &m_ringPool; }
void DebugCore::initialize()
{
    if (m_initialized) return;
    m_initialized = true;
    registerMcuDebugMetaTypes();
    connect(&m_pluginMgr, &PluginManager::errorOccurred, this, &DebugCore::errorOccurred);
    connect(&m_pluginMgr, &PluginManager::sessionEnding, this, &DebugCore::invalidateDataPath);
    connect(&m_pluginMgr, &PluginManager::physicalPreparing, this, [this](IPhysicalPlugin*) { wireDataPath(); });
    connect(&m_pluginMgr, &PluginManager::protocolActivated, this, [this](IProtocolPlugin*) { wireDataPath(); });
    connect(&m_pluginMgr, &PluginManager::physicalActivated, this, [this](IPhysicalPlugin* p) { emit connectionChanged(p->isOpen()); });
    connect(&m_pluginMgr, &PluginManager::physicalDeactivated, this, [this]() { invalidateDataPath(); emit connectionChanged(false); });
}
void DebugCore::enrich(DataFrame& frame)
{
    for (ChannelSample& sample : frame.channels) {
        const QString key = QString::number(sample.index);
        if (!m_channelMetadata.contains(key) && m_channelMetadata.size() >= 1024) continue;
        QVariantMap meta = m_channelMetadata.value(key).toMap();
        if (!sample.name.isEmpty() || !meta.contains("name")) meta.insert("name", sample.name);
        if (!sample.unit.isEmpty() || !meta.contains("unit")) meta.insert("unit", sample.unit);
        const auto overrides = m_channelOverrides.value(key).toMap();
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) meta.insert(it.key(), it.value());
        sample.name = meta.value("name").toString();
        sample.unit = meta.value("unit").toString();
        m_channelMetadata.insert(key, meta);
    }
}
void DebugCore::publish(const DataFrame& frame)
{
    DataFrame enriched = frame;
    enrich(enriched);
    for (const auto& sample : enriched.channels) {
        if (std::isfinite(sample.value) && !m_ringPool.push(sample.index, {enriched.timestamp_us, sample.value}) && !m_historyLimitReported) {
            m_historyLimitReported = true;
            emit errorOccurred(tr("Numeric history limit reached; some new channels are not recorded. Clear history to reset."));
        }
    }
    m_channelHub.dispatch(enriched);
    emit framePublished(enriched);
}
void DebugCore::sendCommand(const QVariantMap& command)
{
    auto* physical = m_pluginMgr.activePhysical();
    if (!physical || !physical->isOpen()) { emit errorOccurred(tr("No active physical connection")); return; }
    auto* protocol = m_pluginMgr.activeProtocol();
    const auto bytes = protocol ? protocol->encodeCommand(command) : command.value("bytes").toByteArray();
    if (bytes.isEmpty()) { emit errorOccurred(tr("Invalid command or unsupported command format")); return; }
    if (physical->write(bytes) != bytes.size()) { emit errorOccurred(tr("Command was not fully accepted into the TX queue; delivery may be partial")); return; }
    DataFrame tx;
    tx.timestamp_us = currentTimestampMicros();
    tx.rawPayload = bytes;
    tx.direction = FrameDirection::Transmit;
    tx.attributes = command;
    tx.attributes.insert("tx_state", "queued");
    publish(tx);
    emit commandSent(bytes);
}
QVariantMap DebugCore::channelMetadata() const
{
    auto result = m_channelMetadata;
    for (auto it = m_channelOverrides.constBegin(); it != m_channelOverrides.constEnd(); ++it) result.insert(it.key(), it.value());
    return result;
}
void DebugCore::setChannelMetadata(ChannelId channel, const QString& name, const QString& unit)
{
    const auto key = QString::number(channel);
    if (!m_channelOverrides.contains(key) && m_channelOverrides.size() >= 1024) return;
    m_channelOverrides.insert(key, QVariantMap{{"name", name}, {"unit", unit}});
}
void DebugCore::setChannelMetadata(const QVariantMap& metadata)
{
    if (metadata.size() > 1024) { emit errorOccurred(tr("Too many channel metadata entries")); return; }
    m_channelOverrides = metadata;
    m_channelMetadata.clear();
}
void DebugCore::clearHistory()
{
    m_ringPool.clear();
    m_historyLimitReported = false;
    for (auto* visual : m_pluginMgr.visualPlugins()) visual->clearHistory();
}
void DebugCore::replayHistory()
{
    for (auto* visual : m_pluginMgr.visualPlugins()) visual->clearNumericHistory();
    for (ChannelId channel : m_ringPool.activeChannels()) {
        const auto samples = m_ringPool.replay(channel, std::numeric_limits<qint64>::min());
        for (const auto& sample : samples) {
            DataFrame frame;
            frame.timestamp_us = sample.timestamp_us;
            frame.channels = {{channel, sample.value}};
            enrich(frame);
            m_channelHub.dispatch(frame); // Display only: never record again or send to the device.
        }
    }
    m_historyLimitReported = false;
}
void DebugCore::invalidateDataPath()
{
    ++m_epoch;
    QObject::disconnect(m_physicalDataConnection);
    QObject::disconnect(m_physicalErrorConnection);
    QObject::disconnect(m_physicalStatusConnection);
    QObject::disconnect(m_protocolFrameConnection);
    m_physicalDataConnection = {}; m_physicalErrorConnection = {};
    m_physicalStatusConnection = {}; m_protocolFrameConnection = {};
}
void DebugCore::wireDataPath()
{
    invalidateDataPath();
    const quint64 epoch = m_epoch;
    QPointer<IPhysicalPlugin> physical = m_pluginMgr.activePhysical();
    QPointer<IProtocolPlugin> protocol = m_pluginMgr.activeProtocol();
    if (physical) {
        m_physicalErrorConnection = connect(physical, &IPhysicalPlugin::errorOccurred, this,
            [this, epoch](const QString& error) { if (epoch == m_epoch) emit errorOccurred(error); }, Qt::QueuedConnection);
        m_physicalStatusConnection = connect(physical, &IPhysicalPlugin::statusChanged, this,
            [this, epoch](bool connected) {
                if (epoch != m_epoch) return;
                if (!connected) m_pluginMgr.deactivateAll(); else emit connectionChanged(true);
            }, Qt::QueuedConnection);
    }
    if (protocol) {
        m_protocolFrameConnection = connect(protocol, &IProtocolPlugin::frameParsed, this,
            [this, epoch](const DataFrame& frame) { if (epoch == m_epoch) publish(frame); });
    }
    if (physical && protocol) {
        m_physicalDataConnection = connect(physical, &IPhysicalPlugin::dataReceived, this,
            [this, epoch, physical, protocol](const QByteArray& bytes) {
                if (epoch != m_epoch || !physical || !protocol || !physical->isOpen()) return;
                protocol->feedBytes(bytes);
            }, Qt::QueuedConnection);
    }
}
