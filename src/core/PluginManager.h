#pragma once

#include "sdk/IControlPlugin.h"
#include "sdk/IPhysicalPlugin.h"
#include "sdk/IProtocolPlugin.h"
#include "sdk/IVisualPlugin.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPluginLoader>
#include <QScopedPointer>
#include <QString>
#include <QVariantMap>

class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    void scanPlugins(const QString& pluginDir);

    QList<IPhysicalPlugin*> physicalPlugins() const;
    QList<IProtocolPlugin*> protocolPlugins() const;
    QList<IVisualPlugin*> visualPlugins() const;
    QList<IControlPlugin*> controlPlugins() const;

    bool activatePhysical(const QString& name, const QVariantMap& config);
    bool activateProtocol(const QString& name);
    void deactivateAll();

    IPhysicalPlugin* activePhysical() const;
    IProtocolPlugin* activeProtocol() const;

signals:
    void physicalActivated(IPhysicalPlugin* plugin);
    void physicalDeactivated();
    void protocolActivated(IProtocolPlugin* plugin);
    void errorOccurred(const QString& message);

private:
    struct LoadedPlugin {
        QScopedPointer<QPluginLoader> loader;
        QString path;
        QJsonObject meta;
        QObject* instance = nullptr;
    };

    void loadPluginFile(const QString& path);
    void registerInstance(QObject* instance, const QJsonObject& meta);
    bool metadataMatchesType(const QJsonObject& meta, const QString& expectedType) const;
    bool metadataSupportsCurrentPlatform(const QJsonObject& meta) const;
    QString currentPlatform() const;

    QList<LoadedPlugin*> m_loaded;
    QList<IPhysicalPlugin*> m_physicalPlugins;
    QList<IProtocolPlugin*> m_protocolPlugins;
    QList<IVisualPlugin*> m_visualPlugins;
    QList<IControlPlugin*> m_controlPlugins;
    IPhysicalPlugin* m_activePhysical = nullptr;
    IProtocolPlugin* m_activeProtocol = nullptr;
};
