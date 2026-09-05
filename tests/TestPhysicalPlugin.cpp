#include "sdk/IPhysicalPlugin.h"
// Test-only plugin: its output directory is deliberately outside the application plugin tree.
class TestPhysicalPlugin : public IPhysicalPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IPhysicalPlugin_iid FILE "test_physical.json")
    Q_INTERFACES(IPhysicalPlugin)
public:
    bool open(const QVariantMap& config) override {
        close();
        if (config.value("fail", false).toBool()) { emit statusChanged(false); return false; }
        opened = true;
        emit statusChanged(true);
        const auto first = config.value("first").toByteArray();
        if (!first.isEmpty()) emit dataReceived(first);
        return true;
    }
    void close() override { if (opened) { opened = false; emit statusChanged(false); } }
    bool isOpen() const override { return opened; }
    qint64 write(const QByteArray& bytes) override { return opened ? bytes.size() : -1; }
    QString name() const override { return QStringLiteral("Test Physical"); }
    QString version() const override { return QStringLiteral("2.0.0"); }
    QVariantMap defaultConfig() const override { return {}; }
private:
    bool opened = false;
};
#include "TestPhysicalPlugin.moc"
