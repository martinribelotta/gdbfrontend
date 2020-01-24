#ifndef DEBUGMANAGER_H
#define DEBUGMANAGER_H

#include <QHash>
#include <QObject>

#include <functional>

namespace gdb {

struct Variable {
    QString name;
    QString type;
    QString value;

    static Variable parseMap(const QVariantMap& data);
};

struct Frame {
    int level;
    QString func;
    quint64 addr;
    QHash<QString, QString> params;
    QString file;
    QString fullpath;
    int line;

    static Frame parseMap(const QVariantMap& data);
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

    static Breakpoint parseMap(const QVariantMap& data);
};

struct Thread {
    int id;
    QString targetId;
    QString details;
    QString name;
    enum State_t { Unknown, Stopped, Running } state;
    Frame frame;
    int core;

    static Thread parseMap(const QVariantMap& data);
};

}

class DebugManager : public QObject
{
    Q_OBJECT

public:
    enum class ResponseAction_t {
        Permanent,
        Temporal
    };

    using ResponseHandler_t = std::function<void (const QVariant& v)>;

    Q_PROPERTY(QString gdbCommand READ gdbCommand WRITE setGdbCommand)
    Q_PROPERTY(bool remote READ isRemote)

    static DebugManager *instance();

    QString gdbCommand() const;
    bool isRemote() const;

public slots:
    void execute();
    void quit();

    void command(const QString& cmd);
    void commandAndResponse(const QString& cmd,
                            const ResponseHandler_t& handler,
                            ResponseAction_t action = ResponseAction_t::Temporal);

    void breakInsert(const QString& path);

    void loadExecutable(const QString& file);
    void launchRemote(const QString& remoteTarget);
    void launchLocal();

    void commandContinue();
    void commandNext();
    void commandStep();
    void commandFinish();
    void commandInterrupt();

    void setGdbCommand(QString gdbCommand);

    void stackListFrames();

signals:
    void started();
    void gdbPromt();
    void targetRemoteConnected();
    void gdbError(const QString& msg);

    void asyncRunning(const QString& thid);
    void asyncStopped(const QString& reason, const gdb::Frame& frame, const QString& thid, int core);

    void updateThreads(int currentId, const QList<gdb::Thread>& threads);
    void updateCurrentFrame(const gdb::Frame& frame);
    void updateStackFrame(const QList<gdb::Frame>& stackFrames);
    void updateLocalVariables(const QList<gdb::Variable>& variableList);

    void breakpointInserted(const gdb::Breakpoint& bp);
    void breakpointModified(const gdb::Breakpoint& bp);
    void breakpointRemoved(int id);

    void result(int token, const QString& reason, const QVariant& results); // <token>^...
    void streamConsole(const QString& text);
    void streamTarget(const QString& text);
    void streamGdb(const QString& text);
    void streamDebugInternal(const QString& text);

private slots:
    void processLine(const QString& line);

private:
    explicit DebugManager(QObject *parent = nullptr);
    virtual ~DebugManager();

    struct Priv_t;
    Priv_t *self;
};

Q_DECLARE_METATYPE(gdb::Variable)
Q_DECLARE_METATYPE(gdb::Frame)
Q_DECLARE_METATYPE(gdb::Breakpoint)
Q_DECLARE_METATYPE(gdb::Thread)

#endif // DEBUGMANAGER_H
