#pragma once

#include "sdk/DataFrame.h"

#include <QList>
#include <QString>
#include <QWidget>

#define IVisualPlugin_iid "org.mcd.sdk.IVisualPlugin/1.0"

class IVisualPlugin : public QWidget {
    Q_OBJECT
public:
    explicit IVisualPlugin(QWidget* parent = nullptr) : QWidget(parent) {}
    ~IVisualPlugin() override = default;

    virtual void onChannelData(const DataFrame& frame) = 0;
    virtual QList<quint16> subscribedChannels() = 0;
    virtual qint64 historyFrom() = 0;
    virtual QString name() const = 0;
};

Q_DECLARE_INTERFACE(IVisualPlugin, IVisualPlugin_iid)
