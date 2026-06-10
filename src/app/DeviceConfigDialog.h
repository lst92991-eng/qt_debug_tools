#pragma once

#include "sdk/IPhysicalPlugin.h"

#include <QDialog>
#include <QLineEdit>
#include <QMap>
#include <QVariantMap>

class DeviceConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit DeviceConfigDialog(IPhysicalPlugin* plugin,
                                const QVariantMap& currentConfig,
                                QWidget* parent = nullptr);

    QVariantMap config() const;

private:
    QMap<QString, QLineEdit*> m_editors;
};
