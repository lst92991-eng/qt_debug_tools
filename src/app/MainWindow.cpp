#include "MainWindow.h"

#include "app/DeviceConfigDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_core(DebugCore::instance())
{
    m_core->initialize();
    buildUi();
    connect(m_core, &DebugCore::errorOccurred, this, [this](const QString& message) {
        statusBar()->showMessage(message, 6000);
    });

    scanPlugins();
    populatePluginUi();
    setConnected(false);
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("MCU Debug Tool"));
    resize(1120, 720);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* connectionRow = new QHBoxLayout;
    m_physicalCombo = new QComboBox(this);
    m_protocolCombo = new QComboBox(this);
    m_configButton = new QPushButton(tr("Config"), this);
    m_connectButton = new QPushButton(tr("Connect"), this);
    m_disconnectButton = new QPushButton(tr("Disconnect"), this);
    m_statusLabel = new QLabel(tr("Disconnected"), this);

    connectionRow->addWidget(new QLabel(tr("Physical:"), this));
    connectionRow->addWidget(m_physicalCombo, 1);
    connectionRow->addWidget(new QLabel(tr("Protocol:"), this));
    connectionRow->addWidget(m_protocolCombo, 1);
    connectionRow->addWidget(m_configButton);
    connectionRow->addWidget(m_connectButton);
    connectionRow->addWidget(m_disconnectButton);
    connectionRow->addWidget(m_statusLabel);
    root->addLayout(connectionRow);

    m_visualTabs = new QTabWidget(this);
    root->addWidget(m_visualTabs, 1);
    setCentralWidget(central);

    auto* dock = new QDockWidget(tr("Controls"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    m_controlTabs = new QTabWidget(dock);
    dock->setWidget(m_controlTabs);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(m_configButton, &QPushButton::clicked, this, &MainWindow::configureSelectedPhysical);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectDevice);
    connect(m_disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectDevice);
}

void MainWindow::scanPlugins()
{
    const QString root = pluginRoot();
    m_core->pluginManager()->scanPlugins(root);
    statusBar()->showMessage(tr("Plugin path: %1").arg(root), 5000);
}

void MainWindow::populatePluginUi()
{
    PluginManager* manager = m_core->pluginManager();

    m_physicalCombo->clear();
    for (IPhysicalPlugin* plugin : manager->physicalPlugins()) {
        m_physicalCombo->addItem(plugin->name());
        m_physicalConfigs.insert(plugin->name(), plugin->defaultConfig());
    }

    m_protocolCombo->clear();
    for (IProtocolPlugin* plugin : manager->protocolPlugins()) {
        m_protocolCombo->addItem(plugin->name());
    }

    m_visualTabs->clear();
    for (IVisualPlugin* plugin : manager->visualPlugins()) {
        m_visualTabs->addTab(plugin, plugin->name());
        m_core->channelHub()->subscribe(plugin, plugin->subscribedChannels());
    }

    m_controlTabs->clear();
    for (IControlPlugin* plugin : manager->controlPlugins()) {
        m_controlTabs->addTab(plugin, plugin->name());
        connect(plugin, &IControlPlugin::commandGenerated, m_core, &DebugCore::sendCommand);
    }

    const bool canConnect = m_physicalCombo->count() > 0 && m_protocolCombo->count() > 0;
    m_connectButton->setEnabled(canConnect);
    m_configButton->setEnabled(m_physicalCombo->count() > 0);
}

void MainWindow::configureSelectedPhysical()
{
    IPhysicalPlugin* plugin = selectedPhysical();
    if (!plugin) {
        return;
    }

    const QString key = plugin->name();
    DeviceConfigDialog dialog(plugin, m_physicalConfigs.value(key, plugin->defaultConfig()), this);
    if (dialog.exec() == QDialog::Accepted) {
        m_physicalConfigs.insert(key, dialog.config());
    }
}

void MainWindow::connectDevice()
{
    IPhysicalPlugin* physical = selectedPhysical();
    IProtocolPlugin* protocol = selectedProtocol();
    if (!physical || !protocol) {
        return;
    }

    PluginManager* manager = m_core->pluginManager();
    if (!manager->activateProtocol(protocol->name())) {
        return;
    }

    QObject::disconnect(m_activeStatusConnection);
    const QVariantMap config = m_physicalConfigs.value(physical->name(), physical->defaultConfig());
    if (!manager->activatePhysical(physical->name(), config)) {
        setConnected(false);
        return;
    }

    m_activeStatusConnection = connect(physical, &IPhysicalPlugin::statusChanged, this, &MainWindow::setConnected);
    setConnected(true);
}

void MainWindow::disconnectDevice()
{
    QObject::disconnect(m_activeStatusConnection);
    m_activeStatusConnection = {};
    m_core->pluginManager()->deactivateAll();
    setConnected(false);
}

void MainWindow::setConnected(bool connected)
{
    m_statusLabel->setText(connected ? tr("Connected") : tr("Disconnected"));
    m_connectButton->setEnabled(!connected && m_physicalCombo->count() > 0 && m_protocolCombo->count() > 0);
    m_disconnectButton->setEnabled(connected);
    m_physicalCombo->setEnabled(!connected);
    m_protocolCombo->setEnabled(!connected);
    m_configButton->setEnabled(!connected && m_physicalCombo->count() > 0);
}

IPhysicalPlugin* MainWindow::selectedPhysical() const
{
    const QString name = m_physicalCombo->currentText();
    for (IPhysicalPlugin* plugin : m_core->pluginManager()->physicalPlugins()) {
        if (plugin->name() == name) {
            return plugin;
        }
    }
    return nullptr;
}

IProtocolPlugin* MainWindow::selectedProtocol() const
{
    const QString name = m_protocolCombo->currentText();
    for (IProtocolPlugin* plugin : m_core->pluginManager()->protocolPlugins()) {
        if (plugin->name() == name) {
            return plugin;
        }
    }
    return nullptr;
}

QString MainWindow::pluginRoot() const
{
    const QString appPlugins = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
    if (QDir(appPlugins).exists()) {
        return appPlugins;
    }

    const QString cwdPlugins = QDir(QDir::currentPath()).filePath(QStringLiteral("plugins"));
    if (QDir(cwdPlugins).exists()) {
        return cwdPlugins;
    }

    return appPlugins;
}
