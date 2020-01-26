#include "debugmanager.h"
#include "widget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#define _tr(text) QApplication::translate(__func__, text)

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("gdbfront");
    QApplication::setApplicationVersion("0.1");
    QCommandLineParser parser;
    parser.setApplicationDescription("GDB Frontend");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("executable", _tr("Executable to debug"));
    parser.addOptions({
        { "init", _tr("Init script file"), "init" },
        { "gdb", _tr("GDB Executable name"), "gdb" },
        { "start", _tr("Automatic start session debug") },
        { "gdbcmd", _tr("GDB Command"), "gdbcmd" }
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

    Widget w;
    w.show();
    return a.exec();
}
