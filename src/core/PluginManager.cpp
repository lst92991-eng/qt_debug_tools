#include "core/PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

#include <utility>

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager()
{
    deactivateAll();
    qDeleteAll(m_loaded);
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

    const QFileInfoList files = root.entryInfoList(filters, QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo& file : files) {
        loadPluginFile(file.absoluteFilePath());
    }

    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dir : dirs) {
        scanPlugins(dir.absoluteFilePath());
    }
}

QList<IPhysicalPlugin*> PluginManager::physicalPlugins() const
{
    return m_physicalPlugins;
}

QList<IProtocolPlugin*> PluginManager::protocolPlugins() const
{
    return m_protocolPlugins;
}

QList<IVisualPlugin*> PluginManager::visualPlugins() const
{
    return m_visualPlugins;
}

QList<IControlPlugin*> PluginManager::controlPlugins() const
{
    return m_controlPlugins;
}

bool PluginManager::activatePhysical(const QString& name, const QVariantMap& config)
{
    for (IPhysicalPlugin* plugin : m_physicalPlugins) {
        if (plugin->name() != name) {
            continue;
        }

        if (m_activePhysical && m_activePhysical != plugin) {
            m_activePhysical->close();
            emit physicalDeactivated();
        }

        if (!plugin->open(config)) {
            emit errorOccurred(tr("Failed to open physical plugin: %1").arg(name));
            return false;
        }

        m_activePhysical = plugin;
        emit physicalActivated(plugin);
        return true;
    }

    emit errorOccurred(tr("Physical plugin not found: %1").arg(name));
    return false;
}

bool PluginManager::activateProtocol(const QString& name)
{
    for (IProtocolPlugin* plugin : m_protocolPlugins) {
        if (plugin->name() == name) {
            m_activeProtocol = plugin;
            emit protocolActivated(plugin);
            return true;
        }
    }

    emit errorOccurred(tr("Protocol plugin not found: %1").arg(name));
    return false;
}

void PluginManager::deactivateAll()
{
    if (m_activePhysical) {
        m_activePhysical->close();
        m_activePhysical = nullptr;
        emit physicalDeactivated();
    }
    m_activeProtocol = nullptr;
}

IPhysicalPlugin* PluginManager::activePhysical() const
{
    return m_activePhysical;
}

IProtocolPlugin* PluginManager::activeProtocol() const
{
    return m_activeProtocol;
}

void PluginManager::loadPluginFile(const QString& path)
{
    for (LoadedPlugin* loaded : std::as_const(m_loaded)) {
        if (loaded->path == path) {
            return;
        }
    }

    auto* loaded = new LoadedPlugin;
    loaded->loader.reset(new QPluginLoader(path));
    loaded->path = path;
    loaded->meta = loaded->loader->metaData().value("MetaData").toObject();
    if (!metadataSupportsCurrentPlatform(loaded->meta)) {
        delete loaded;
        return;
    }

    loaded->instance = loaded->loader->instance();

    if (!loaded->instance) {
        emit errorOccurred(tr("Failed to load %1: %2")
                               .arg(QFileInfo(path).fileName(), loaded->loader->errorString()));
        delete loaded;
        return;
    }

    registerInstance(loaded->instance, loaded->meta);
    m_loaded.append(loaded);
}

void PluginManager::registerInstance(QObject* instance, const QJsonObject& meta)
{
    if (auto* plugin = qobject_cast<IPhysicalPlugin*>(instance)) {
        if (metadataMatchesType(meta, "physical")) {
            m_physicalPlugins.append(plugin);
        }
        return;
    }

    if (auto* plugin = qobject_cast<IProtocolPlugin*>(instance)) {
        if (metadataMatchesType(meta, "protocol")) {
            m_protocolPlugins.append(plugin);
        }
        return;
    }

    if (auto* plugin = qobject_cast<IVisualPlugin*>(instance)) {
        if (metadataMatchesType(meta, "visual")) {
            m_visualPlugins.append(plugin);
        }
        return;
    }

    if (auto* plugin = qobject_cast<IControlPlugin*>(instance)) {
        if (metadataMatchesType(meta, "control")) {
            m_controlPlugins.append(plugin);
        }
        return;
    }

    emit errorOccurred(tr("Loaded object does not implement a known plugin interface: %1")
                           .arg(instance->objectName()));
}

bool PluginManager::metadataMatchesType(const QJsonObject& meta, const QString& expectedType) const
{
    const QString type = meta.value("type").toString();
    if (type.isEmpty() || type == expectedType) {
        return true;
    }
    emit const_cast<PluginManager*>(this)->errorOccurred(
        tr("Plugin metadata type mismatch: expected %1, got %2").arg(expectedType, type));
    return false;
}

bool PluginManager::metadataSupportsCurrentPlatform(const QJsonObject& meta) const
{
    const QJsonArray platforms = meta.value(QStringLiteral("platforms")).toArray();
    if (platforms.isEmpty()) {
        return true;
    }

    const QString platform = currentPlatform();
    for (const QJsonValue& value : platforms) {
        const QString entry = value.toString().toLower();
        if (entry == platform || entry == QStringLiteral("all")) {
            return true;
        }
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
