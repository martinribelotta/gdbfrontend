#ifndef DEBUGMANAGER_H
#define DEBUGMANAGER_H

#include <QHash>
#include <QObject>

namespace gdb {

struct Frame {
    int level;
    QString func;
    quint64 addr;
    QHash<QString, QString> params;
    QString file;
    QString fullpath;
    int line;
};

struct Breakpoint {
    int number;
    QString type;
    QString disp;
    bool enable;
    quint64 addr;
    QString func;
    QString file;
    QString fullname;
    int line;
    QList<QString> threadGroups;
    int times;
    QString originalLocation;
};

}

class DebugManager : public QObject
{
    Q_OBJECT

public:
    Q_PROPERTY(QString gdbCommand READ gdbCommand WRITE setGdbCommand)
    Q_PROPERTY(bool remote READ isRemote)

    static DebugManager *instance();

    QString gdbCommand() const { return m_gdbCommand; }
    bool isRemote() const { return m_remote; }

public slots:
    void execute();
    void quit();

    void command(const QString& cmd);

    void breakInsert(const QString& path);

    void loadExecutable(const QString& file);
    void launchRemote(const QString& remoteTarget);
    void launchLocal();

    void commandContinue();
    void commandNext();
    void commandStep();
    void commandFinish();
    void commandInterrupt();

    void setGdbCommand(QString gdbCommand) { m_gdbCommand = gdbCommand; }

signals:
    void started();
    void asyncRunning(const QString& thid);
    void asyncStopped(const QString& reason, const gdb::Frame& frame, const QString& thid, int core);

    void gdbPromnt();

    void resultDone(const QHash<QString, QVariant>& results);
    void resultDoneFrame(const gdb::Frame& frame);
    void resultDoneStackFrame(const QList<gdb::Frame>& stackFrame);
    void resultDoneBreakpoint(const gdb::Breakpoint& bp);

    void resultError(const QString& msg);
    void resultRunning();
    void resultConnected();
    void resultExit();

    void streamConsole(const QString& text);
    void streamTarget(const QString& text);
    void streamGdb(const QString& text);

private slots:
    void processLine(const QString& line);

private:
    explicit DebugManager(QObject *parent = nullptr);
    virtual ~DebugManager();

    struct Priv_t;
    Priv_t *self;
    QString m_gdbCommand;
    bool m_remote;
};

Q_DECLARE_METATYPE(gdb::Frame)
Q_DECLARE_METATYPE(gdb::Breakpoint)

#endif // DEBUGMANAGER_H
