#include "MainWindow.h"

#include "app/DeviceConfigDialog.h"

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
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

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

MainWindow::~MainWindow()
{
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
    fileMenu->addAction(tr("Save History"), this, &MainWindow::saveHistory);
    fileMenu->addAction(tr("Load History"), this, &MainWindow::loadHistory);
    fileMenu->addAction(tr("Clear History"), this, &MainWindow::clearHistory);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Exit"), qApp, &QApplication::quit);

    auto* pluginMenu = menuBar()->addMenu(tr("Plugins"));
    pluginMenu->addAction(tr("Rescan"), this, [this]() {
        detachPluginPages();
        m_core->pluginManager()->clear();
        scanPlugins();
        populatePluginUi();
    });

    auto* toolsMenu = menuBar()->addMenu(tr("Tools"));
    toolsMenu->addAction(tr("Channel Map"), this, &MainWindow::editChannelMap);
    toolsMenu->addAction(tr("Serial Baud Scan"), this, &MainWindow::scanSerialBaudRates);

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

void MainWindow::saveSession()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save Session"),
        QDir::home().filePath(QStringLiteral("mcu_debug_session.json")),
        tr("MCU Debug Session (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("physical"), m_physicalCombo->currentText());
    root.insert(QStringLiteral("protocol"), m_protocolCombo->currentText());
    root.insert(QStringLiteral("channel_metadata"), QJsonObject::fromVariantMap(m_core->channelMetadata()));

    QJsonObject configs;
    for (auto it = m_physicalConfigs.constBegin(); it != m_physicalConfigs.constEnd(); ++it) {
        configs.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
    }
    root.insert(QStringLiteral("physical_configs"), configs);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusBar()->showMessage(tr("Failed to save session: %1").arg(file.errorString()), 6000);
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    statusBar()->showMessage(tr("Session saved"), 3000);
}

void MainWindow::loadSession()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Load Session"),
        QDir::homePath(),
        tr("MCU Debug Session (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("Failed to load session: %1").arg(file.errorString()), 6000);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonObject configs = root.value(QStringLiteral("physical_configs")).toObject();
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        m_physicalConfigs.insert(it.key(), it.value().toObject().toVariantMap());
    }
    m_core->setChannelMetadata(root.value(QStringLiteral("channel_metadata")).toObject().toVariantMap());

    const int physicalIdx = m_physicalCombo->findText(root.value(QStringLiteral("physical")).toString());
    if (physicalIdx >= 0) {
        m_physicalCombo->setCurrentIndex(physicalIdx);
    }
    const int protocolIdx = m_protocolCombo->findText(root.value(QStringLiteral("protocol")).toString());
    if (protocolIdx >= 0) {
        m_protocolCombo->setCurrentIndex(protocolIdx);
    }
    statusBar()->showMessage(tr("Session loaded"), 3000);
}

void MainWindow::saveHistory()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save History"),
        QDir::home().filePath(QStringLiteral("mcu_debug_history.mcdr")),
        tr("MCU Debug History (*.mcdr)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!m_core->ringBufferPool()->saveToFile(path, &error)) {
        statusBar()->showMessage(tr("Failed to save history: %1").arg(error), 6000);
        return;
    }
    statusBar()->showMessage(tr("History saved"), 3000);
}

void MainWindow::loadHistory()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Load History"),
        QDir::homePath(),
        tr("MCU Debug History (*.mcdr)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!m_core->ringBufferPool()->loadFromFile(path, &error)) {
        statusBar()->showMessage(tr("Failed to load history: %1").arg(error), 6000);
        return;
    }
    statusBar()->showMessage(tr("History loaded"), 3000);
}

void MainWindow::clearHistory()
{
    m_core->ringBufferPool()->clear();
    statusBar()->showMessage(tr("History cleared"), 3000);
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

    QSet<quint16> channels;
    for (quint16 channel : m_core->ringBufferPool()->activeChannels()) {
        channels.insert(channel);
    }
    const QVariantMap metadata = m_core->channelMetadata();
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        bool ok = false;
        const quint16 channel = static_cast<quint16>(it.key().toUShort(&ok));
        if (ok) {
            channels.insert(channel);
        }
    }

    QList<quint16> sortedChannels = channels.values();
    std::sort(sortedChannels.begin(), sortedChannels.end());
    table->setRowCount(sortedChannels.size());
    for (int row = 0; row < sortedChannels.size(); ++row) {
        const quint16 channel = sortedChannels.at(row);
        const QVariantMap meta = metadata.value(QString::number(channel)).toMap();
        auto* indexItem = new QTableWidgetItem(QString::number(channel));
        indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, indexItem);
        table->setItem(row, 1, new QTableWidgetItem(meta.value(QStringLiteral("name")).toString()));
        table->setItem(row, 2, new QTableWidgetItem(meta.value(QStringLiteral("unit")).toString()));
    }
    table->resizeColumnsToContents();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    for (int row = 0; row < table->rowCount(); ++row) {
        const auto* indexItem = table->item(row, 0);
        if (!indexItem) {
            continue;
        }
        const quint16 channel = static_cast<quint16>(indexItem->text().toUShort());
        const QString name = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
        const QString unit = table->item(row, 2) ? table->item(row, 2)->text().trimmed() : QString();
        m_core->setChannelMetadata(channel, name, unit);
    }
    statusBar()->showMessage(tr("Channel map updated"), 3000);
}

void MainWindow::scanSerialBaudRates()
{
    const QList<qint32> baudRates = {
        9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000
    };

    QString report;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (ports.isEmpty()) {
        report = tr("No serial ports found.");
    }

    for (const QSerialPortInfo& info : ports) {
        report += tr("Port %1 (%2)\n").arg(info.portName(), info.description());
        for (qint32 baud : baudRates) {
            QSerialPort port(info);
            port.setBaudRate(baud);
            port.setDataBits(QSerialPort::Data8);
            port.setParity(QSerialPort::NoParity);
            port.setStopBits(QSerialPort::OneStop);
            port.setFlowControl(QSerialPort::NoFlowControl);
            if (port.open(QIODevice::ReadWrite)) {
                report += tr("  %1: open ok\n").arg(baud);
                port.close();
            } else {
                report += tr("  %1: %2\n").arg(baud).arg(port.errorString());
            }
        }
        report += QLatin1Char('\n');
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Serial Baud Scan"));
    dialog.resize(640, 480);
    auto* root = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(report);
    root->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}

void MainWindow::detachPluginPages()
{
    if (m_visualTabs) {
        while (m_visualTabs->count() > 0) {
            QWidget* page = m_visualTabs->widget(0);
            m_visualTabs->removeTab(0);
            if (page) {
                page->setParent(nullptr);
            }
        }
    }

    if (m_controlTabs) {
        while (m_controlTabs->count() > 0) {
            QWidget* page = m_controlTabs->widget(0);
            m_controlTabs->removeTab(0);
            if (page) {
                page->setParent(nullptr);
            }
        }
    }
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
