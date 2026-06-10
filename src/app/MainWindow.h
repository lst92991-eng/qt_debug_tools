#pragma once

#include "core/DebugCore.h"

#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTabWidget>
#include <QVariantMap>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void scanPlugins();
    void populatePluginUi();
    void configureSelectedPhysical();
    void connectDevice();
    void disconnectDevice();
    void setConnected(bool connected);
    IPhysicalPlugin* selectedPhysical() const;
    IProtocolPlugin* selectedProtocol() const;
    QString pluginRoot() const;

    DebugCore* m_core = nullptr;
    QComboBox* m_physicalCombo = nullptr;
    QComboBox* m_protocolCombo = nullptr;
    QPushButton* m_configButton = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPushButton* m_disconnectButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTabWidget* m_visualTabs = nullptr;
    QTabWidget* m_controlTabs = nullptr;
    QHash<QString, QVariantMap> m_physicalConfigs;
    QMetaObject::Connection m_activeStatusConnection;
};
