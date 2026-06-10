#include "app/MainWindow.h"

#include "core/DebugCore.h"

#include <QApplication>
#include <QDir>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        DebugCore* core = DebugCore::instance();
        core->initialize();

        const QString pluginRoot = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
        core->pluginManager()->scanPlugins(pluginRoot);

        QTextStream out(stdout);
        out << "pluginRoot=" << pluginRoot << '\n';
        out << "physical=" << core->pluginManager()->physicalPlugins().size() << '\n';
        out << "protocol=" << core->pluginManager()->protocolPlugins().size() << '\n';
        out << "visual=" << core->pluginManager()->visualPlugins().size() << '\n';
        out << "control=" << core->pluginManager()->controlPlugins().size() << '\n';

        const bool ok = !core->pluginManager()->physicalPlugins().isEmpty()
            && !core->pluginManager()->protocolPlugins().isEmpty()
            && !core->pluginManager()->visualPlugins().isEmpty()
            && !core->pluginManager()->controlPlugins().isEmpty();
        return ok ? 0 : 2;
    }

    MainWindow window;
    window.show();

    return app.exec();
}
