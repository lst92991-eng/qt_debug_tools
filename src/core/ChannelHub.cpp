#include "core/ChannelHub.h"
#include <QPointer>
#include <QVector>
#include <utility>
ChannelHub::ChannelHub(QObject* parent) : QObject(parent) {}
void ChannelHub::subscribe(IVisualPlugin* plugin, const QList<ChannelId>& channels)
{
    if (!plugin) return;
    unsubscribe(plugin);
    m_destroyConnections.insert(plugin, connect(plugin, &QObject::destroyed, this, [this, plugin]() { unsubscribe(plugin); }));
    if (channels.isEmpty()) m_wildcardSubscribers.insert(plugin);
    else for (ChannelId channel : channels) m_subscriptions[channel].insert(plugin);
}
void ChannelHub::unsubscribe(IVisualPlugin* plugin)
{
    if (!plugin) return;
    QObject::disconnect(m_destroyConnections.take(plugin));
    m_wildcardSubscribers.remove(plugin);
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end();) {
        it->remove(plugin);
        if (it->isEmpty()) it = m_subscriptions.erase(it);
        else ++it;
    }
}
void ChannelHub::dispatch(const DataFrame& frame)
{
    QSet<IVisualPlugin*> targets = m_wildcardSubscribers;
    for (const auto& sample : frame.channels) {
        const auto it = m_subscriptions.constFind(sample.index);
        if (it != m_subscriptions.constEnd()) targets.unite(*it);
    }
    QVector<QPointer<IVisualPlugin>> guarded;
    guarded.reserve(targets.size());
    for (auto* plugin : std::as_const(targets)) guarded.push_back(plugin);
    for (const auto& plugin : std::as_const(guarded)) {
        if (plugin && m_destroyConnections.contains(plugin.data())) plugin->onChannelData(frame);
    }
}
