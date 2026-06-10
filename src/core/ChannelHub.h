#pragma once

#include "sdk/DataFrame.h"
#include "sdk/IVisualPlugin.h"

#include <QHash>
#include <QObject>
#include <QSet>

class ChannelHub : public QObject {
    Q_OBJECT
public:
    explicit ChannelHub(QObject* parent = nullptr);

    void subscribe(IVisualPlugin* plugin, const QList<quint16>& channels);
    void unsubscribe(IVisualPlugin* plugin);
    void dispatch(const DataFrame& frame);

private:
    QHash<quint16, QSet<IVisualPlugin*>> m_subscriptions;
    QSet<IVisualPlugin*> m_wildcardSubscribers;
};
