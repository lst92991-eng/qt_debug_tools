#include "GaugePlugin.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

GaugePlugin::GaugePlugin(QWidget* parent)
    : IVisualPlugin(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    auto* row = new QHBoxLayout;
    m_selector = new QComboBox(this);
    row->addWidget(new QLabel(tr("Channel:"), this));
    row->addWidget(m_selector, 1);
    root->addLayout(row);
    root->addStretch(1);
    connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), this, qOverload<>(&GaugePlugin::update));
}

void GaugePlugin::onChannelData(const DataFrame& frame)
{
    bool changed = false;
    for (const ChannelSample& sample : frame.channels) {
        if (std::isnan(sample.value)) {
            continue;
        }
        const bool isNew = !m_values.contains(sample.index);
        m_values.insert(sample.index, sample.value);
        m_labels.insert(sample.index, sample.name.isEmpty() ? QStringLiteral("CH%1").arg(sample.index) : sample.name);
        changed = changed || isNew;
    }
    if (changed) {
        rebuildSelector();
    }
    update();
}

QList<quint16> GaugePlugin::subscribedChannels()
{
    return {};
}

qint64 GaugePlugin::historyFrom()
{
    return 0;
}

QString GaugePlugin::name() const
{
    return QStringLiteral("Gauge");
}

void GaugePlugin::paintEvent(QPaintEvent* event)
{
    IVisualPlugin::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(250, 251, 252));

    if (m_values.isEmpty()) {
        painter.setPen(QColor(95, 99, 104));
        painter.drawText(rect(), Qt::AlignCenter, tr("No numeric channel data"));
        return;
    }

    const quint16 channel = selectedChannel();
    const double value = m_values.value(channel, 0.0);
    const double clamped = std::clamp(value, 0.0, 100.0);

    const QPoint center(width() / 2, height() / 2 + 40);
    const int radius = std::max(60, std::min(width(), height()) / 3);
    const QRect arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);

    painter.setPen(QPen(QColor(218, 223, 230), 16, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arcRect, 30 * 16, 120 * 16);
    painter.setPen(QPen(QColor(44, 110, 203), 16, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arcRect, 150 * 16, static_cast<int>(-120.0 * clamped / 100.0 * 16));

    constexpr double pi = 3.14159265358979323846;
    const double angle = (150.0 - 120.0 * clamped / 100.0) * pi / 180.0;
    const QPointF needleEnd(center.x() + std::cos(angle) * (radius - 18),
                            center.y() - std::sin(angle) * (radius - 18));
    painter.setPen(QPen(QColor(34, 39, 46), 3));
    painter.drawLine(center, needleEnd);
    painter.setBrush(QColor(34, 39, 46));
    painter.drawEllipse(center, 5, 5);

    painter.setPen(QColor(34, 39, 46));
    painter.setFont(QFont(painter.font().family(), 20, QFont::Bold));
    painter.drawText(QRect(0, center.y() + 18, width(), 36), Qt::AlignCenter, QString::number(value, 'f', 2));
    painter.setFont(QFont(painter.font().family(), 10));
    painter.drawText(QRect(0, center.y() + 54, width(), 26), Qt::AlignCenter, m_labels.value(channel));
}

void GaugePlugin::rebuildSelector()
{
    const QVariant current = m_selector->currentData();
    const QSignalBlocker blocker(m_selector);
    m_selector->clear();
    QList<quint16> channels = m_values.keys();
    std::sort(channels.begin(), channels.end());
    for (quint16 channel : channels) {
        m_selector->addItem(m_labels.value(channel, QStringLiteral("CH%1").arg(channel)), channel);
    }
    const int idx = m_selector->findData(current);
    if (idx >= 0) {
        m_selector->setCurrentIndex(idx);
    }
}

quint16 GaugePlugin::selectedChannel() const
{
    return static_cast<quint16>(m_selector->currentData().toUInt());
}
