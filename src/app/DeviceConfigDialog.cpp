#include "DeviceConfigDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

DeviceConfigDialog::DeviceConfigDialog(IPhysicalPlugin* plugin,
                                       const QVariantMap& currentConfig,
                                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Device Configuration"));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    root->addLayout(form);

    QVariantMap values = plugin ? plugin->defaultConfig() : QVariantMap();
    for (auto it = currentConfig.constBegin(); it != currentConfig.constEnd(); ++it) {
        values.insert(it.key(), it.value());
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        auto* edit = new QLineEdit(this);
        edit->setText(it.value().toString());
        form->addRow(it.key(), edit);
        m_editors.insert(it.key(), edit);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QVariantMap DeviceConfigDialog::config() const
{
    QVariantMap result;
    for (auto it = m_editors.constBegin(); it != m_editors.constEnd(); ++it) {
        result.insert(it.key(), it.value()->text().trimmed());
    }
    return result;
}
