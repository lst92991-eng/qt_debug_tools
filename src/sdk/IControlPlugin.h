#pragma once

#include <QVariantMap>
#include <QWidget>

#define IControlPlugin_iid "org.mcd.sdk.IControlPlugin/1.0"

class IControlPlugin : public QWidget {
    Q_OBJECT
public:
    explicit IControlPlugin(QWidget* parent = nullptr) : QWidget(parent) {}
    ~IControlPlugin() override = default;

    virtual QString name() const = 0;

signals:
    void commandGenerated(const QVariantMap& command);
};

Q_DECLARE_INTERFACE(IControlPlugin, IControlPlugin_iid)
