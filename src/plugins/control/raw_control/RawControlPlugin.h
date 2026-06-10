#pragma once

#include "sdk/IControlPlugin.h"

#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QToolButton>

class RawControlPlugin : public IControlPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IControlPlugin_iid FILE "raw_control.json")
    Q_INTERFACES(IControlPlugin)

public:
    explicit RawControlPlugin(QWidget* parent = nullptr);

    QString name() const override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Preset {
        QString name;
        QString hex;
    };

    void buildUi();
    void validateInput();
    void sendCurrent(bool periodic = false);
    void addHistory(const QString& hex);
    QByteArray parseHex(const QString& text) const;
    QString normalizeHex(const QString& text) const;
    bool isValidHex(const QString& text) const;
    void loadPresets();
    void savePresets() const;
    void refreshPresetList();
    QString presetsPath() const;
    void addPreset();
    void removeSelectedPreset();
    void sendPreset(QListWidgetItem* item);

    QLineEdit* m_input = nullptr;
    QToolButton* m_sendButton = nullptr;
    QToolButton* m_periodicButton = nullptr;
    QSpinBox* m_interval = nullptr;
    QListWidget* m_presetsList = nullptr;
    QTimer m_periodicTimer;
    QStringList m_history;
    int m_historyIndex = -1;
    QVector<Preset> m_presets;
};
