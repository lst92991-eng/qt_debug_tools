#pragma once

#include "sdk/IVisualPlugin.h"

#include <QComboBox>
#include <QHash>

class GaugePlugin : public IVisualPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IVisualPlugin_iid FILE "gauge.json")
    Q_INTERFACES(IVisualPlugin)

public:
    explicit GaugePlugin(QWidget* parent = nullptr);

    void onChannelData(const DataFrame& frame) override;
    QList<ChannelId> subscribedChannels() override;
    qint64 historyFrom() override;
    QString name() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuildSelector();
    ChannelId selectedChannel() const;

    QComboBox* m_selector = nullptr;
    QHash<ChannelId, double> m_values;
    QHash<ChannelId, QString> m_labels;
};
