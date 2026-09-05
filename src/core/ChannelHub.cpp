#include "core/ChannelHub.h"

#include <utility>

ChannelHub::ChannelHub(QObject* parent)
    : QObject(parent)
{
}

void ChannelHub::subscribe(IVisualPlugin* plugin, const QList<quint16>& channels)
{
    if (!plugin) {
        return;
    }

    unsubscribe(plugin);
    m_destroyConnections.insert(
        plugin,
        connect(plugin, &QObject::destroyed, this, [this, plugin]() {
            m_wildcardSubscribers.remove(plugin);
            for (auto it = m_subscriptions.begin(); it != m_subscriptions.end();) {
                it->remove(plugin);
                if (it->isEmpty()) {
                    it = m_subscriptions.erase(it);
                } else {
                    ++it;
                }
            }
            m_destroyConnections.remove(plugin);
        }));

    if (channels.isEmpty()) {
        m_wildcardSubscribers.insert(plugin);
        return;
    }

    for (quint16 channel : channels) {
        m_subscriptions[channel].insert(plugin);
    }
}

void ChannelHub::unsubscribe(IVisualPlugin* plugin)
{
    if (!plugin) {
        return;
    }

    const auto connectionIt = m_destroyConnections.find(plugin);
    if (connectionIt != m_destroyConnections.end()) {
        QObject::disconnect(connectionIt.value());
        m_destroyConnections.erase(connectionIt);
    }

    m_wildcardSubscribers.remove(plugin);
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end();) {
        it->remove(plugin);
        if (it->isEmpty()) {
            it = m_subscriptions.erase(it);
        } else {
            ++it;
        }
    }
}

void ChannelHub::dispatch(const DataFrame& frame)
{
    QSet<IVisualPlugin*> targets = m_wildcardSubscribers;

    for (const ChannelSample& sample : frame.channels) {
        const auto it = m_subscriptions.constFind(sample.index);
        if (it != m_subscriptions.constEnd()) {
            targets.unite(*it);
        }
    }

    for (IVisualPlugin* plugin : std::as_const(targets)) {
        if (plugin) {
            plugin->onChannelData(frame);
        }
    }
}
