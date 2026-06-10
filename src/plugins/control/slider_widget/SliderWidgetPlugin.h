#pragma once

#include "sdk/IControlPlugin.h"

#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

class SliderWidgetPlugin : public IControlPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IControlPlugin_iid FILE "slider_widget.json")
    Q_INTERFACES(IControlPlugin)

public:
    explicit SliderWidgetPlugin(QWidget* parent = nullptr);

    QString name() const override;

private:
    void buildUi();
    void emitValue(int value);
    QByteArray encodeValue(int value) const;

    QSpinBox* m_channel = nullptr;
    QSpinBox* m_min = nullptr;
    QSpinBox* m_max = nullptr;
    QSpinBox* m_value = nullptr;
    QSlider* m_slider = nullptr;
    QComboBox* m_width = nullptr;
    QToolButton* m_sendButton = nullptr;
    QToolButton* m_liveButton = nullptr;
};
