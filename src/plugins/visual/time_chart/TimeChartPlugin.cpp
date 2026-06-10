#include "TimeChartPlugin.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

TimeChartPlugin::TimeChartPlugin(QWidget* parent)
    : IVisualPlugin(parent)
{
    buildUi();
    connect(&m_updateTimer, &QTimer::timeout, this, qOverload<>(&TimeChartPlugin::update));
    m_updateTimer.start(33);
}

void TimeChartPlugin::onChannelData(const DataFrame& frame)
{
    bool changed = false;
    for (const ChannelSample& sample : frame.channels) {
        if (std::isnan(sample.value)) {
            continue;
        }

        QVector<Point>& points = m_series[sample.index];
        points.push_back({frame.timestamp_us, sample.value});
        if (!sample.name.isEmpty()) {
            m_labels.insert(sample.index, sample.name);
        } else if (!m_labels.contains(sample.index)) {
            m_labels.insert(sample.index, QStringLiteral("CH%1").arg(sample.index));
        }
        changed = true;
    }

    if (changed) {
        trimSeries();
        const QSignalBlocker blocker(m_channelSelector);
        const QString current = m_channelSelector->currentData().toString();
        m_channelSelector->clear();
        m_channelSelector->addItem(tr("All"), QString());
        QList<quint16> channels = m_series.keys();
        std::sort(channels.begin(), channels.end());
        for (quint16 channel : channels) {
            m_channelSelector->addItem(m_labels.value(channel, QStringLiteral("CH%1").arg(channel)), channel);
        }
        const int idx = m_channelSelector->findData(current);
        if (idx >= 0) {
            m_channelSelector->setCurrentIndex(idx);
        }
    }
}

QList<quint16> TimeChartPlugin::subscribedChannels()
{
    return {};
}

qint64 TimeChartPlugin::historyFrom()
{
    return 0;
}

QString TimeChartPlugin::name() const
{
    return QStringLiteral("Time Chart");
}

void TimeChartPlugin::paintEvent(QPaintEvent* event)
{
    IVisualPlugin::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect plot = rect().adjusted(44, 48, -16, -34);
    painter.fillRect(rect(), QColor(250, 251, 252));
    painter.setPen(QColor(210, 215, 222));
    painter.drawRect(plot);

    const QList<quint16> channels = visibleChannels();
    if (channels.isEmpty()) {
        painter.setPen(QColor(95, 99, 104));
        painter.drawText(plot, Qt::AlignCenter, tr("No numeric channel data"));
        return;
    }

    qint64 newest = 0;
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    for (quint16 channel : channels) {
        const QVector<Point>& points = m_series.value(channel);
        for (const Point& point : points) {
            newest = std::max(newest, point.ts);
            minValue = std::min(minValue, point.value);
            maxValue = std::max(maxValue, point.value);
        }
    }

    const qint64 start = m_followLatest->isChecked() ? newest - m_windowUs : 0;
    if (!std::isfinite(minValue) || !std::isfinite(maxValue)) {
        return;
    }
    if (qFuzzyCompare(minValue, maxValue)) {
        minValue -= 1.0;
        maxValue += 1.0;
    }

    painter.setPen(QColor(225, 229, 233));
    for (int i = 1; i < 5; ++i) {
        const int y = plot.top() + plot.height() * i / 5;
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    const QVector<QColor> colors = {
        QColor(44, 110, 203),
        QColor(20, 150, 90),
        QColor(196, 95, 35),
        QColor(140, 82, 185),
        QColor(34, 134, 148)
    };

    int colorIndex = 0;
    for (quint16 channel : channels) {
        const QVector<Point>& points = m_series.value(channel);
        QPainterPath path;
        bool started = false;
        for (const Point& point : points) {
            if (m_followLatest->isChecked() && point.ts < start) {
                continue;
            }
            const double xRatio = m_followLatest->isChecked()
                ? static_cast<double>(point.ts - start) / static_cast<double>(m_windowUs)
                : static_cast<double>(point.ts - points.first().ts) / std::max<qint64>(1, newest - points.first().ts);
            const double yRatio = (point.value - minValue) / (maxValue - minValue);
            const QPointF pt(plot.left() + xRatio * plot.width(), plot.bottom() - yRatio * plot.height());
            if (!started) {
                path.moveTo(pt);
                started = true;
            } else {
                path.lineTo(pt);
            }
        }
        painter.setPen(QPen(colors.at(colorIndex % colors.size()), 2.0));
        painter.drawPath(path);
        ++colorIndex;
    }

    painter.setPen(QColor(58, 64, 73));
    painter.drawText(8, plot.top() + 12, QString::number(maxValue, 'f', 2));
    painter.drawText(8, plot.bottom(), QString::number(minValue, 'f', 2));
    painter.drawText(plot.left(), height() - 10, tr("%1 s window").arg(m_windowUs / 1'000'000));
}

void TimeChartPlugin::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* toolbar = new QHBoxLayout;
    m_channelSelector = new QComboBox(this);
    m_channelSelector->addItem(tr("All"), QString());
    m_followLatest = new QCheckBox(tr("Follow"), this);
    m_followLatest->setChecked(true);
    toolbar->addWidget(new QLabel(tr("Channel:"), this));
    toolbar->addWidget(m_channelSelector, 1);
    toolbar->addWidget(m_followLatest);
    root->addLayout(toolbar);
    root->addStretch(1);

    connect(m_channelSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, qOverload<>(&TimeChartPlugin::update));
    connect(m_followLatest, &QCheckBox::toggled, this, qOverload<>(&TimeChartPlugin::update));
}

void TimeChartPlugin::trimSeries()
{
    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        QVector<Point>& points = it.value();
        if (points.size() > m_capacityPerChannel) {
            points.remove(0, points.size() - m_capacityPerChannel);
        }
    }
}

QList<quint16> TimeChartPlugin::visibleChannels() const
{
    const QVariant selected = m_channelSelector->currentData();
    if (selected.isValid() && !selected.toString().isEmpty()) {
        return {static_cast<quint16>(selected.toUInt())};
    }

    QList<quint16> channels = m_series.keys();
    std::sort(channels.begin(), channels.end());
    return channels;
}
