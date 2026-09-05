#pragma once

#include "sdk/IVisualPlugin.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QTimer>
#include <QToolButton>

class TimeChartPlugin : public IVisualPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IVisualPlugin_iid FILE "time_chart.json")
    Q_INTERFACES(IVisualPlugin)

public:
    explicit TimeChartPlugin(QWidget* parent = nullptr);

    void onChannelData(const DataFrame& frame) override;
    QList<ChannelId> subscribedChannels() override;
    qint64 historyFrom() override;
    QString name() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Point {
        qint64 ts = 0;
        double value = 0.0;
    };

    void buildUi();
    void trimSeries();
    QList<ChannelId> visibleChannels() const;

    QHash<ChannelId, QVector<Point>> m_series;
    QHash<ChannelId, QString> m_labels;
    QComboBox* m_channelSelector = nullptr;
    QCheckBox* m_followLatest = nullptr;
    QTimer m_updateTimer;
    qint64 m_windowUs = 10'000'000;
    int m_capacityPerChannel = 20000;
};
