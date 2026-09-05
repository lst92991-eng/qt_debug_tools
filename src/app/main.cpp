#include "app/MainWindow.h"
#include "core/DebugCore.h"
#include <QApplication>
#include <QDir>
#include <QTextStream>
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        auto* core = DebugCore::instance();
        core->initialize();
        const auto root = QDir(QCoreApplication::applicationDirPath()).filePath("plugins");
        auto* manager = core->pluginManager();
        manager->scanPlugins(root);
        QTextStream out(stdout);
        out << "pluginRoot=" << root << '\n'
            << "physical=" << manager->physicalPlugins().size() << '\n'
            << "protocol=" << manager->protocolPlugins().size() << '\n'
            << "visual=" << manager->visualPlugins().size() << '\n'
            << "control=" << manager->controlPlugins().size() << '\n';
        const bool ok = !manager->physicalPlugins().isEmpty() && manager->protocolPlugins().size() == 2
            && manager->visualPlugins().size() == 3 && manager->controlPlugins().size() == 2;
        manager->clear(); // Destroy QWidget plugins before QApplication goes away.
        return ok ? 0 : 2;
    }
    MainWindow window;
    window.show();
    return app.exec();
}
