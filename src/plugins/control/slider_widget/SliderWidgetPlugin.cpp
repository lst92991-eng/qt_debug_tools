#include "SliderWidgetPlugin.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <limits>
SliderWidgetPlugin::SliderWidgetPlugin(QWidget* parent) : IControlPlugin(parent) { buildUi(); }
QString SliderWidgetPlugin::name() const { return QStringLiteral("Slider Widget"); }
void SliderWidgetPlugin::buildUi()
{
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_channel = new QSpinBox(this);
    m_channel->setRange(0, 65535);
    m_channel->setValue(1);
    m_min = new QSpinBox(this);
    m_min->setRange(0, 255);
    m_max = new QSpinBox(this);
    m_max->setRange(0, 255);
    m_max->setValue(255);
    m_value = new QSpinBox(this);
    m_value->setObjectName("sliderValue");
    m_width = new QComboBox(this);
    m_width->setObjectName("payloadWidth");
    m_width->addItem(tr("uint8"), 1);
    m_width->addItem(tr("int16 LE"), 2);
    m_width->addItem(tr("int32 LE"), 4);
    form->addRow(tr("Channel"), m_channel);
    form->addRow(tr("Min"), m_min);
    form->addRow(tr("Max"), m_max);
    form->addRow(tr("Value"), m_value);
    form->addRow(tr("Payload"), m_width);
    root->addLayout(form);
    m_slider = new QSlider(Qt::Horizontal, this);
    root->addWidget(m_slider);
    auto* buttons = new QHBoxLayout;
    m_sendButton = new QToolButton(this);
    m_sendButton->setObjectName("sendValue");
    m_sendButton->setText(tr("Send"));
    m_liveButton = new QToolButton(this);
    m_liveButton->setObjectName("liveValue");
    m_liveButton->setText(tr("Live"));
    m_liveButton->setCheckable(true);
    buttons->addWidget(m_sendButton);
    buttons->addWidget(m_liveButton);
    buttons->addStretch();
    root->addLayout(buttons);
    root->addStretch();
    const auto syncRange = [this]() {
        stop(); // A configuration change must not emit a control command implicitly.
        const QSignalBlocker b1(m_min), b2(m_max), b3(m_value), b4(m_slider);
        if (m_min->value() > m_max->value()) m_max->setValue(m_min->value());
        m_value->setRange(m_min->value(), m_max->value());
        m_slider->setRange(m_min->value(), m_max->value());
        m_slider->setValue(m_value->value());
    };
    const auto applyBounds = [this, syncRange]() {
        stop();
        int low = std::numeric_limits<int>::min();
        int high = std::numeric_limits<int>::max();
        if (m_width->currentData().toInt() == 1) { low = 0; high = 255; }
        if (m_width->currentData().toInt() == 2) { low = -32768; high = 32767; }
        const QSignalBlocker b1(m_min), b2(m_max);
        m_min->setRange(low, high);
        m_max->setRange(low, high);
        syncRange();
    };
    connect(m_min, qOverload<int>(&QSpinBox::valueChanged), this, syncRange);
    connect(m_max, qOverload<int>(&QSpinBox::valueChanged), this, syncRange);
    connect(m_width, qOverload<int>(&QComboBox::currentIndexChanged), this, [applyBounds](int) { applyBounds(); });
    connect(m_slider, &QSlider::valueChanged, m_value, &QSpinBox::setValue);
    connect(m_value, qOverload<int>(&QSpinBox::valueChanged), m_slider, &QSlider::setValue);
    connect(m_value, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) { if (m_liveButton->isChecked()) emitValue(value); });
    connect(m_sendButton, &QToolButton::clicked, this, [this]() { emitValue(m_value->value()); });
    applyBounds();
}
void SliderWidgetPlugin::emitValue(int value)
{
    const auto bytes = encodeValue(value);
    if (bytes.isEmpty()) return;
    emit commandGenerated({{"channel", m_channel->value()}, {"value", value}, {"bytes", bytes}, {"source", "slider_widget"}});
}
QByteArray SliderWidgetPlugin::encodeValue(int value) const
{
    const int width = m_width->currentData().toInt();
    if ((width != 1 && width != 2 && width != 4)
        || (width == 1 && (value < 0 || value > 255))
        || (width == 2 && (value < -32768 || value > 32767))) return {};
    QByteArray bytes = QByteArray::fromHex("534c");
    bytes.append(char((m_channel->value() >> 8) & 0xff));
    bytes.append(char(m_channel->value() & 0xff));
    const quint32 encoded = quint32(qint32(value));
    for (int i = 0; i < width; ++i) bytes.append(char((encoded >> (i * 8)) & 0xff));
    return bytes;
}
