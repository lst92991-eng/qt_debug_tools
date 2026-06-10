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
    QList<quint16> subscribedChannels() override;
    qint64 historyFrom() override;
    QString name() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuildSelector();
    quint16 selectedChannel() const;

    QComboBox* m_selector = nullptr;
    QHash<quint16, double> m_values;
    QHash<quint16, QString> m_labels;
};
