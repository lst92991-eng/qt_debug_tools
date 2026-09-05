#pragma once

#include "sdk/DataFrame.h"
#include "sdk/IVisualPlugin.h"

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QSet>

class ChannelHub : public QObject {
    Q_OBJECT
public:
    explicit ChannelHub(QObject* parent = nullptr);

    void subscribe(IVisualPlugin* plugin, const QList<ChannelId>& channels);
    void unsubscribe(IVisualPlugin* plugin);
    void dispatch(const DataFrame& frame);

private:
    QHash<ChannelId, QSet<IVisualPlugin*>> m_subscriptions;
    QSet<IVisualPlugin*> m_wildcardSubscribers;
    QHash<IVisualPlugin*, QMetaObject::Connection> m_destroyConnections;
};
