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
TimeChartPlugin::TimeChartPlugin(QWidget* parent) : IVisualPlugin(parent)
{
    buildUi();
    connect(&m_updateTimer, &QTimer::timeout, this, qOverload<>(&TimeChartPlugin::update));
    m_updateTimer.start(33);
}
void TimeChartPlugin::onChannelData(const DataFrame& frame)
{
    if (frame.timestamp_us < 0) return;
    bool selectorChanged = false, channelsChanged = false;
    for (const auto& sample : frame.channels) {
        if (!std::isfinite(sample.value)) continue;
        const bool isNew = !m_series.contains(sample.index);
        if (isNew && m_series.size() >= 1024) continue;
        auto& points = m_series[sample.index];
        points.push_back({frame.timestamp_us, sample.value});
        const int limit = std::min(m_capacityPerChannel, 1000000 / std::max(1, int(m_series.size())));
        if (points.size() > limit) points.remove(0, points.size() - limit);
        const auto label = sample.name.isEmpty() ? m_labels.value(sample.index, QStringLiteral("CH%1").arg(sample.index)) : sample.name;
        if (isNew || m_labels.value(sample.index) != label) { m_labels.insert(sample.index, label); selectorChanged = true; }
        channelsChanged = channelsChanged || isNew;
    }
    if (channelsChanged) trimSeries();
    if (!selectorChanged) return;
    const QSignalBlocker blocker(m_channelSelector);
    const auto selected = m_channelSelector->currentData();
    m_channelSelector->clear();
    m_channelSelector->addItem(tr("All"), QVariant());
    auto channels = m_series.keys();
    std::sort(channels.begin(), channels.end());
    for (ChannelId channel : channels) m_channelSelector->addItem(m_labels.value(channel), QVariant::fromValue<qulonglong>(channel));
    const int index = m_channelSelector->findData(selected);
    if (index >= 0) m_channelSelector->setCurrentIndex(index);
}
QList<ChannelId> TimeChartPlugin::subscribedChannels() { return {}; }
qint64 TimeChartPlugin::historyFrom() { return 0; }
QString TimeChartPlugin::name() const { return QStringLiteral("Time Chart"); }
void TimeChartPlugin::clearHistory()
{
    m_series.clear();
    m_labels.clear();
    const QSignalBlocker blocker(m_channelSelector);
    m_channelSelector->clear();
    m_channelSelector->addItem(tr("All"), QVariant());
    update();
}
void TimeChartPlugin::paintEvent(QPaintEvent* event)
{
    IVisualPlugin::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(250, 251, 252));
    const QRect plot = rect().adjusted(44, 48, -16, -34);
    if (plot.width() <= 0 || plot.height() <= 0) return;
    painter.setPen(QColor(210, 215, 222));
    painter.drawRect(plot);
    const auto channels = visibleChannels();
    qint64 newest = -1, oldest = std::numeric_limits<qint64>::max();
    for (ChannelId channel : channels) {
        const auto points = m_series.value(channel);
        for (const auto& point : points) { newest = std::max(newest, point.ts); oldest = std::min(oldest, point.ts); }
    }
    if (oldest > newest) { painter.drawText(plot, Qt::AlignCenter, tr("No numeric channel data")); return; }
    const qint64 start = m_followLatest->isChecked() ? newest - m_windowUs : oldest;
    const qint64 span = m_followLatest->isChecked() ? m_windowUs : std::max<qint64>(1, newest - start);
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (ChannelId channel : channels) {
        const auto points = m_series.value(channel);
        for (const auto& point : points) {
            if (point.ts < start) continue;
            low = std::min(low, point.value); high = std::max(high, point.value);
        }
    }
    if (!std::isfinite(low) || !std::isfinite(high)) return;
    const double scale = std::max({std::abs(low), std::abs(high), 1.0});
    double lowScaled = low / scale, highScaled = high / scale;
    if (qFuzzyCompare(lowScaled, highScaled) || lowScaled == highScaled) { lowScaled -= 1; highScaled += 1; }
    painter.setPen(QColor(225, 229, 233));
    for (int i = 1; i < 5; ++i) {
        const int y = plot.top() + plot.height() * i / 5;
        painter.drawLine(plot.left(), y, plot.right(), y);
    }
    const QVector<QColor> colors = {QColor(44,110,203), QColor(20,150,90), QColor(196,95,35), QColor(140,82,185), QColor(34,134,148)};
    int color = 0;
    painter.save();
    painter.setClipRect(plot);
    for (ChannelId channel : channels) {
        const auto points = m_series.value(channel);
        QPainterPath path;
        bool started = false;
        for (const auto& point : points) {
            if (point.ts < start) continue;
            const double x = double(point.ts - start) / double(span);
            const double y = (point.value / scale - lowScaled) / (highScaled - lowScaled);
            const QPointF position(plot.left() + x * plot.width(), plot.bottom() - y * plot.height());
            if (!started) { path.moveTo(position); started = true; } else path.lineTo(position);
        }
        painter.setPen(QPen(colors.at(color++ % colors.size()), 2.0));
        painter.drawPath(path);
    }
    painter.restore();
    painter.setPen(QColor(58,64,73));
    painter.drawText(8, plot.top() + 12, QString::number(high, 'g', 5));
    painter.drawText(8, plot.bottom(), QString::number(low, 'g', 5));
    painter.drawText(plot.left(), height() - 10, tr("%1 s window (bounded history)").arg(span / 1000000.0));
}
void TimeChartPlugin::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8,8,8,8);
    auto* toolbar = new QHBoxLayout;
    m_channelSelector = new QComboBox(this);
    m_channelSelector->setObjectName("chartChannelSelector");
    m_channelSelector->addItem(tr("All"), QVariant());
    m_followLatest = new QCheckBox(tr("Follow"), this);
    m_followLatest->setChecked(true);
    toolbar->addWidget(new QLabel(tr("Channel:"), this));
    toolbar->addWidget(m_channelSelector, 1);
    toolbar->addWidget(m_followLatest);
    root->addLayout(toolbar);
    root->addStretch();
    connect(m_channelSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, qOverload<>(&TimeChartPlugin::update));
    connect(m_followLatest, &QCheckBox::toggled, this, qOverload<>(&TimeChartPlugin::update));
}
void TimeChartPlugin::trimSeries()
{
    const int limit = std::min(m_capacityPerChannel, 1000000 / std::max(1, int(m_series.size())));
    for (auto it = m_series.begin(); it != m_series.end(); ++it)
        if (it->size() > limit) it->remove(0, it->size() - limit);
}
QList<ChannelId> TimeChartPlugin::visibleChannels() const
{
    const auto selected = m_channelSelector->currentData();
    if (selected.isValid()) return {selected.toULongLong()};
    auto channels = m_series.keys();
    std::sort(channels.begin(), channels.end());
    return channels;
}
