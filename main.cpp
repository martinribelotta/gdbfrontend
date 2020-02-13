#include "debugmanager.h"
#include "mainwidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("gdbfront");
    QApplication::setApplicationDisplayName(QApplication::tr("GDBFront"));
    QApplication::setApplicationVersion("0.1");
    QApplication::setOrganizationName("ourEmbeddeds");
    QApplication::setOrganizationDomain("www.ourembeddeds.com");
    QCommandLineParser parser;
    parser.setApplicationDescription("GDB Frontend");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("exec", QApplication::tr("Executable to debug"));
    parser.addOptions({
        { "init", QApplication::tr("Init script file"), "init" },
        { "gdb", QApplication::tr("GDB Executable name"), "gdb" },
        { "start", QApplication::tr("Automatic start session debug") },
        { "gdbcmd", QApplication::tr("GDB Command"), "gdbcmd" }
    });
    parser.process(a);

    QStringList gdbArgv;
    auto g = DebugManager::instance();
    if (parser.isSet("gdb"))
        g->setGdbCommand(parser.value("gdb"));
    if (parser.isSet("init"))
        gdbArgv.append({ "-x", parser.value("init") });
    if (parser.isSet("gdbcmd"))
        for (const auto& s: parser.values("gdbcmd"))
            gdbArgv.append({ "-ex", s });
    for (const auto& cmd: parser.positionalArguments())
        gdbArgv.append(cmd);
    if (!gdbArgv.isEmpty())
        g->setGdbArgs(gdbArgv);
    if (parser.isSet("start"))
        QTimer::singleShot(0, g, &DebugManager::execute);

    MainWidget w;
    w.show();
    return a.exec();
}
