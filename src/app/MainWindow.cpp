#include "MainWindow.h"
#include "app/DeviceConfigDialog.h"
#include "core/SessionIO.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_core(DebugCore::instance())
{
    m_core->initialize();
    buildUi();
    connect(m_core, &DebugCore::errorOccurred, this, [this](const QString& message) { statusBar()->showMessage(message, 6000); });
    connect(m_core, &DebugCore::connectionChanged, this, &MainWindow::setConnected);
    scanPlugins();
    populatePluginUi();
    setConnected(false);
}
MainWindow::~MainWindow()
{
    disconnectDevice();
    detachPluginPages();
    m_core->pluginManager()->clear();
}
void MainWindow::buildUi()
{
    setWindowTitle(tr("MCU Debug Tool"));
    resize(1120, 720);
    auto* fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(tr("Save Session"), this, &MainWindow::saveSession);
    fileMenu->addAction(tr("Load Session"), this, &MainWindow::loadSession);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Save Numeric History"), this, &MainWindow::saveHistory);
    fileMenu->addAction(tr("Load Numeric History"), this, &MainWindow::loadHistory);
    fileMenu->addAction(tr("Clear History and Views"), this, &MainWindow::clearHistory);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Exit"), qApp, &QApplication::quit);
    auto* pluginMenu = menuBar()->addMenu(tr("Plugins"));
    pluginMenu->addAction(tr("Rescan"), this, [this]() {
        disconnectDevice();
        detachPluginPages();
        m_core->pluginManager()->clear();
        scanPlugins();
        populatePluginUi();
        setConnected(false);
    });
    auto* toolsMenu = menuBar()->addMenu(tr("Tools"));
    toolsMenu->addAction(tr("Channel Map"), this, &MainWindow::editChannelMap);
    toolsMenu->addAction(tr("Serial Port Open Check"), this, &MainWindow::scanSerialBaudRates);
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    auto* row = new QHBoxLayout;
    m_physicalCombo = new QComboBox(this);
    m_protocolCombo = new QComboBox(this);
    m_configButton = new QPushButton(tr("Config"), this);
    m_connectButton = new QPushButton(tr("Connect"), this);
    m_disconnectButton = new QPushButton(tr("Disconnect"), this);
    m_statusLabel = new QLabel(tr("Disconnected"), this);
    row->addWidget(new QLabel(tr("Physical:"), this));
    row->addWidget(m_physicalCombo, 1);
    row->addWidget(new QLabel(tr("Protocol:"), this));
    row->addWidget(m_protocolCombo, 1);
    row->addWidget(m_configButton);
    row->addWidget(m_connectButton);
    row->addWidget(m_disconnectButton);
    row->addWidget(m_statusLabel);
    root->addLayout(row);
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
    m_core->pluginManager()->scanPlugins(pluginRoot());
}
void MainWindow::populatePluginUi()
{
    auto* manager = m_core->pluginManager();
    m_physicalCombo->clear();
    for (auto* plugin : manager->physicalPlugins()) {
        m_physicalCombo->addItem(plugin->name());
        if (!m_physicalConfigs.contains(plugin->name())) m_physicalConfigs.insert(plugin->name(), plugin->defaultConfig());
    }
    m_protocolCombo->clear();
    for (auto* plugin : manager->protocolPlugins()) m_protocolCombo->addItem(plugin->name());
    m_visualTabs->clear();
    for (auto* plugin : manager->visualPlugins()) {
        m_visualTabs->addTab(plugin, plugin->name());
        m_core->channelHub()->subscribe(plugin, plugin->subscribedChannels());
    }
    m_controlTabs->clear();
    for (auto* plugin : manager->controlPlugins()) {
        m_controlTabs->addTab(plugin, plugin->name());
        connect(plugin, &IControlPlugin::commandGenerated, m_core, &DebugCore::sendCommand, Qt::UniqueConnection);
    }
}
void MainWindow::configureSelectedPhysical()
{
    auto* plugin = selectedPhysical();
    if (!plugin) return;
    DeviceConfigDialog dialog(plugin, m_physicalConfigs.value(plugin->name(), plugin->defaultConfig()), this);
    if (dialog.exec() == QDialog::Accepted) m_physicalConfigs.insert(plugin->name(), dialog.config());
}
void MainWindow::connectDevice()
{
    auto* physical = selectedPhysical();
    auto* protocol = selectedProtocol();
    if (!physical || !protocol) return;
    auto* manager = m_core->pluginManager();
    if (!manager->activateProtocol(protocol->name())) return;
    const bool ok = manager->activatePhysical(physical->name(), m_physicalConfigs.value(physical->name(), physical->defaultConfig()));
    setConnected(ok && physical->isOpen());
}
void MainWindow::disconnectDevice()
{
    m_core->pluginManager()->deactivateAll();
    setConnected(false);
}
void MainWindow::saveSession()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Save Session"), QDir::home().filePath("mcu_debug_session.json"), tr("MCU Debug Session (*.json)"));
    if (path.isEmpty()) return;
    QJsonObject root;
    root.insert("version", 2);
    root.insert("physical", m_physicalCombo->currentText());
    root.insert("protocol", m_protocolCombo->currentText());
    root.insert("channel_metadata", QJsonObject::fromVariantMap(m_core->channelMetadata()));
    QJsonObject configs;
    for (auto it = m_physicalConfigs.constBegin(); it != m_physicalConfigs.constEnd(); ++it) configs.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
    root.insert("physical_configs", configs);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString error;
    QJsonObject validated;
    if (!SessionIO::parse(bytes, &validated, &error)) { statusBar()->showMessage(error, 6000); return; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        statusBar()->showMessage(tr("Failed to save session: %1").arg(file.errorString()), 6000);
        return;
    }
    statusBar()->showMessage(tr("Session saved"), 3000);
}
void MainWindow::loadSession()
{
    if (m_core->pluginManager()->activePhysical()) { statusBar()->showMessage(tr("Disconnect before loading a session"), 6000); return; }
    const auto path = QFileDialog::getOpenFileName(this, tr("Load Session"), QDir::homePath(), tr("MCU Debug Session (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { statusBar()->showMessage(file.errorString(), 6000); return; }
    if (file.size() > SessionIO::MaxBytes) { statusBar()->showMessage(tr("Session file is too large"), 6000); return; }
    QJsonObject root;
    QString error;
    if (!SessionIO::parse(file.readAll(), &root, &error)) { statusBar()->showMessage(error, 6000); return; }
    const int physicalIndex = m_physicalCombo->findText(root.value("physical").toString());
    const int protocolIndex = m_protocolCombo->findText(root.value("protocol").toString());
    if (physicalIndex < 0 || protocolIndex < 0) { statusBar()->showMessage(tr("Session requires unavailable plugins; nothing changed"), 6000); return; }
    auto configs = m_physicalConfigs;
    const auto saved = root.value("physical_configs").toObject();
    for (auto it = saved.begin(); it != saved.end(); ++it) configs.insert(it.key(), it.value().toObject().toVariantMap());
    m_physicalConfigs = std::move(configs);
    m_core->setChannelMetadata(root.value("channel_metadata").toObject().toVariantMap());
    m_physicalCombo->setCurrentIndex(physicalIndex);
    m_protocolCombo->setCurrentIndex(protocolIndex);
    statusBar()->showMessage(tr("Session loaded; connection remains closed"), 3000);
}
void MainWindow::saveHistory()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Save Numeric History"), QDir::home().filePath("mcu_debug_history.mcdr"), tr("MCU Numeric History (*.mcdr)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_core->ringBufferPool()->saveToFile(path, &error)) { statusBar()->showMessage(error, 6000); return; }
    statusBar()->showMessage(tr("Numeric history saved (use Raw Viewer export for packet logs)"), 6000);
}
void MainWindow::loadHistory()
{
    if (m_core->pluginManager()->activePhysical()) { statusBar()->showMessage(tr("Disconnect before loading numeric history"), 6000); return; }
    const auto path = QFileDialog::getOpenFileName(this, tr("Load Numeric History"), QDir::homePath(), tr("MCU Numeric History (*.mcdr)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_core->ringBufferPool()->loadFromFile(path, &error)) { statusBar()->showMessage(error, 6000); return; }
    m_core->replayHistory();
    statusBar()->showMessage(tr("Numeric history loaded and displayed"), 3000);
}
void MainWindow::clearHistory()
{
    m_core->clearHistory();
    statusBar()->showMessage(tr("History and views cleared"), 3000);
}
void MainWindow::editChannelMap()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Channel Map"));
    dialog.resize(520, 420);
    auto* root = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({tr("Index"), tr("Name"), tr("Unit")});
    root->addWidget(table, 1);
    QSet<ChannelId> channels;
    for (ChannelId channel : m_core->ringBufferPool()->activeChannels()) channels.insert(channel);
    const auto metadata = m_core->channelMetadata();
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        bool ok = false;
        const ChannelId channel = it.key().toULongLong(&ok);
        if (ok) channels.insert(channel);
    }
    auto sorted = channels.values();
    std::sort(sorted.begin(), sorted.end());
    table->setRowCount(int(sorted.size()));
    for (int row = 0; row < sorted.size(); ++row) {
        const ChannelId channel = sorted.at(row);
        const auto meta = metadata.value(QString::number(channel)).toMap();
        auto* index = new QTableWidgetItem(QString::number(channel));
        index->setFlags(index->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, index);
        table->setItem(row, 1, new QTableWidgetItem(meta.value("name").toString()));
        table->setItem(row, 2, new QTableWidgetItem(meta.value("unit").toString()));
    }
    table->resizeColumnsToContents();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    for (int row = 0; row < table->rowCount(); ++row) {
        bool ok = false;
        const ChannelId channel = table->item(row, 0)->text().toULongLong(&ok);
        if (!ok) continue;
        m_core->setChannelMetadata(channel, table->item(row, 1)->text().trimmed().left(256), table->item(row, 2)->text().trimmed().left(256));
    }
    statusBar()->showMessage(tr("Channel map updated"), 3000);
}
void MainWindow::scanSerialBaudRates()
{
    if (m_core->pluginManager()->activePhysical()) { statusBar()->showMessage(tr("Disconnect before probing ports"), 6000); return; }
    const QList<qint32> rates = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000};
    QString report = tr("This checks local port opening only. It does not detect the peer baud rate.\n\n");
    const auto ports = QSerialPortInfo::availablePorts();
    if (ports.isEmpty()) report += tr("No serial ports found.\n");
    for (const auto& info : ports) {
        report += tr("Port %1 (%2)\n").arg(info.portName(), info.description());
        for (qint32 baud : rates) {
            QSerialPort port(info);
            if (port.setBaudRate(baud) && port.open(QIODevice::ReadWrite)) {
                report += tr("  %1: open ok\n").arg(baud);
                port.close();
            } else report += tr("  %1: %2\n").arg(baud).arg(port.errorString());
        }
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Serial Port Open Check"));
    dialog.resize(640, 480);
    auto* root = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(report);
    root->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}
void MainWindow::detachPluginPages()
{
    for (auto* plugin : m_core->pluginManager()->visualPlugins()) m_core->channelHub()->unsubscribe(plugin);
    for (auto* plugin : m_core->pluginManager()->controlPlugins()) {
        plugin->stop();
        disconnect(plugin, &IControlPlugin::commandGenerated, m_core, &DebugCore::sendCommand);
    }
    for (auto* tabs : {m_visualTabs, m_controlTabs}) {
        while (tabs && tabs->count() > 0) {
            auto* page = tabs->widget(0);
            tabs->removeTab(0);
            if (page) page->setParent(nullptr);
        }
    }
}
void MainWindow::setConnected(bool connected)
{
    if (!connected) m_core->pluginManager()->stopControls();
    m_statusLabel->setText(connected ? tr("Connected") : tr("Disconnected"));
    m_connectButton->setEnabled(!connected && m_physicalCombo->count() > 0 && m_protocolCombo->count() > 0);
    m_disconnectButton->setEnabled(connected);
    m_physicalCombo->setEnabled(!connected);
    m_protocolCombo->setEnabled(!connected);
    m_configButton->setEnabled(!connected && m_physicalCombo->count() > 0);
    m_controlTabs->setEnabled(connected);
}
IPhysicalPlugin* MainWindow::selectedPhysical() const
{
    for (auto* plugin : m_core->pluginManager()->physicalPlugins()) if (plugin->name() == m_physicalCombo->currentText()) return plugin;
    return nullptr;
}
IProtocolPlugin* MainWindow::selectedProtocol() const
{
    for (auto* plugin : m_core->pluginManager()->protocolPlugins()) if (plugin->name() == m_protocolCombo->currentText()) return plugin;
    return nullptr;
}
QString MainWindow::pluginRoot() const
{
    const auto appPlugins = QDir(QCoreApplication::applicationDirPath()).filePath("plugins");
    if (QDir(appPlugins).exists()) return appPlugins;
    const auto cwdPlugins = QDir::current().filePath("plugins");
    return QDir(cwdPlugins).exists() ? cwdPlugins : appPlugins;
}
