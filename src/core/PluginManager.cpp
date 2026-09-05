#include "core/PluginManager.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>
#include <utility>

PluginManager::PluginManager(QObject* parent) : QObject(parent) {}
PluginManager::~PluginManager() { clear(); }
void PluginManager::stopControls()
{
    for (auto* control : std::as_const(m_controlPlugins)) control->stop();
}
void PluginManager::clear()
{
    deactivateAll();
    m_physicalPlugins.clear();
    m_protocolPlugins.clear();
    m_visualPlugins.clear();
    m_controlPlugins.clear();
    for (LoadedPlugin* loaded : std::as_const(m_loaded)) {
        if (loaded->loader && !loaded->loader->unload())
            emit errorOccurred(tr("Plugin unload failed: %1").arg(loaded->loader->errorString()));
    }
    qDeleteAll(m_loaded);
    m_loaded.clear();
}
void PluginManager::scanPlugins(const QString& pluginDir)
{
    const QDir root(pluginDir);
    if (!root.exists()) {
        emit errorOccurred(tr("Plugin directory does not exist: %1").arg(pluginDir));
        return;
    }
    const QStringList filters = {
#if defined(Q_OS_WIN)
        "*.dll"
#elif defined(Q_OS_MACOS)
        "*.dylib"
#else
        "*.so"
#endif
    };
    for (const QFileInfo& file : root.entryInfoList(filters, QDir::Files | QDir::NoSymLinks))
        loadPluginFile(file.absoluteFilePath());
    for (const QFileInfo& dir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
        scanPlugins(dir.absoluteFilePath());
}
QList<IPhysicalPlugin*> PluginManager::physicalPlugins() const { return m_physicalPlugins; }
QList<IProtocolPlugin*> PluginManager::protocolPlugins() const { return m_protocolPlugins; }
QList<IVisualPlugin*> PluginManager::visualPlugins() const { return m_visualPlugins; }
QList<IControlPlugin*> PluginManager::controlPlugins() const { return m_controlPlugins; }
IPhysicalPlugin* PluginManager::activePhysical() const { return m_activePhysical; }
IProtocolPlugin* PluginManager::activeProtocol() const { return m_activeProtocol; }

bool PluginManager::activatePhysical(const QString& name, const QVariantMap& config)
{
    for (auto* plugin : std::as_const(m_physicalPlugins)) {
        if (plugin->name() != name) continue;
        emit sessionEnding();
        stopControls();
        auto* old = m_activePhysical;
        m_activePhysical = nullptr;
        if (old) old->close();
        emit physicalDeactivated();
        if (m_activeProtocol) m_activeProtocol->reset();
        m_activePhysical = plugin;
        emit physicalPreparing(plugin);
        if (!plugin->open(config)) {
            emit sessionEnding();
            m_activePhysical = nullptr;
            plugin->close();
            emit physicalDeactivated();
            emit errorOccurred(tr("Failed to open physical plugin: %1").arg(name));
            return false;
        }
        emit physicalActivated(plugin);
        return true;
    }
    emit errorOccurred(tr("Physical plugin not found: %1").arg(name));
    return false;
}
bool PluginManager::activateProtocol(const QString& name)
{
    for (auto* plugin : std::as_const(m_protocolPlugins)) {
        if (plugin->name() != name) continue;
        if (m_activeProtocol == plugin && m_activePhysical && m_activePhysical->isOpen()) return true;
        deactivateAll();
        plugin->reset();
        m_activeProtocol = plugin;
        emit protocolActivated(plugin);
        return true;
    }
    emit errorOccurred(tr("Protocol plugin not found: %1").arg(name));
    return false;
}
void PluginManager::deactivateAll()
{
    // Invalidate queued callbacks before closing, resetting, or destroying anything.
    emit sessionEnding();
    stopControls();
    auto* physical = m_activePhysical;
    auto* protocol = m_activeProtocol;
    m_activePhysical = nullptr;
    m_activeProtocol = nullptr;
    if (physical) physical->close();
    if (protocol) protocol->reset();
    emit physicalDeactivated();
}
void PluginManager::loadPluginFile(const QString& path)
{
    for (auto* loaded : std::as_const(m_loaded)) if (loaded->path == path) return;
    auto* loaded = new LoadedPlugin;
    loaded->loader.reset(new QPluginLoader(path));
    loaded->path = path;
    const QJsonObject metadata = loaded->loader->metaData();
    const QString iid = metadata.value("IID").toString();
    const QStringList accepted = {IPhysicalPlugin_iid, IProtocolPlugin_iid, IVisualPlugin_iid, IControlPlugin_iid};
    if (!accepted.contains(iid)) {
        emit errorOccurred(tr("Incompatible plugin SDK: %1 (rebuild all plugins)").arg(path));
        delete loaded;
        return;
    }
    loaded->meta = metadata.value("MetaData").toObject();
    if (!metadataSupportsCurrentPlatform(loaded->meta)) { delete loaded; return; }
    loaded->instance = loaded->loader->instance();
    if (!loaded->instance) {
        emit errorOccurred(tr("Failed to load %1: %2").arg(QFileInfo(path).fileName(), loaded->loader->errorString()));
        delete loaded;
        return;
    }
    registerInstance(loaded->instance, loaded->meta);
    m_loaded.append(loaded);
}
void PluginManager::registerInstance(QObject* instance, const QJsonObject& meta)
{
    if (auto* p = qobject_cast<IPhysicalPlugin*>(instance)) {
        if (metadataMatchesType(meta, "physical")) m_physicalPlugins.append(p);
    } else if (auto* p = qobject_cast<IProtocolPlugin*>(instance)) {
        if (metadataMatchesType(meta, "protocol")) m_protocolPlugins.append(p);
    } else if (auto* p = qobject_cast<IVisualPlugin*>(instance)) {
        if (metadataMatchesType(meta, "visual")) m_visualPlugins.append(p);
    } else if (auto* p = qobject_cast<IControlPlugin*>(instance)) {
        if (metadataMatchesType(meta, "control")) m_controlPlugins.append(p);
    } else {
        emit errorOccurred(tr("Unknown plugin interface: %1").arg(instance->objectName()));
    }
}
bool PluginManager::metadataMatchesType(const QJsonObject& meta, const QString& expectedType) const
{
    const QString type = meta.value("type").toString();
    if (type.isEmpty() || type == expectedType) return true;
    emit const_cast<PluginManager*>(this)->errorOccurred(tr("Plugin metadata type mismatch: %1").arg(type));
    return false;
}
bool PluginManager::metadataSupportsCurrentPlatform(const QJsonObject& meta) const
{
    const QJsonArray platforms = meta.value("platforms").toArray();
    if (platforms.isEmpty()) return true;
    for (const QJsonValue& value : platforms) {
        const QString entry = value.toString().toLower();
        if (entry == currentPlatform() || entry == "all") return true;
    }
    return false;
}
QString PluginManager::currentPlatform() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}
