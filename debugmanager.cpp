#include "debugmanager.h"

#include <QProcess>
#include <QVariant>
#include <QMultiMap>
#include <QTextCodec>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>

#include <csignal>

namespace mi {

constexpr auto DEFAULT_GDB_COMMAND = "gdb";

#ifdef Q_OS_WIN
constexpr auto EOL = "\r\n";
#else
constexpr auto EOL = "\n";
#endif

QString escapedText(const QString& s)
{
    return QString{s}.replace(QRegularExpression{R"(\\(.))"}, "\1");
}

// Partial code is from pygdbmi
// See on: https://github.com/cs01/pygdbmi/blob/master/pygdbmi/gdbmiparser.py

struct {
    const QRegularExpression::PatternOption DOTALL = QRegularExpression::DotMatchesEverythingOption;
    QRegularExpression compile(const QString& text,
                               QRegularExpression::PatternOption flags=QRegularExpression::NoPatternOption) {
        return QRegularExpression{text, QRegularExpression::MultilineOption | flags};
    }
} re;

// GDB machine interface output patterns to match
// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Stream-Records.html#GDB_002fMI-Stream-Records

// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Result-Records.html#GDB_002fMI-Result-Records
// In addition to a number of out-of-band notifications,
// the response to a gdb/mi command includes one of the following result indications:
// done, running, connected, error, exit
const auto _GDB_MI_RESULT_RE = re.compile(R"(^(\d*)\^(\S+?)(?:,(.*))?$)");

// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Async-Records.html#GDB_002fMI-Async-Records
// Async records are used to notify the gdb/mi client of additional
// changes that have occurred. Those changes can either be a consequence
// of gdb/mi commands (e.g., a breakpoint modified) or a result of target activity
// (e.g., target stopped).
const auto _GDB_MI_NOTIFY_RE = re.compile(R"(^(\d*)[*=](\S+?),(.*)$)");

// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Stream-Records.html#GDB_002fMI-Stream-Records
// "~" string-output
// The console output stream contains text that should be displayed
// in the CLI console window. It contains the textual responses to CLI commands.
const auto _GDB_MI_CONSOLE_RE = re.compile(R"re(~"(.*)")re", re.DOTALL);

// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Stream-Records.html#GDB_002fMI-Stream-Records
// "&" string-output
// The log stream contains debugging messages being produced by gdb's internals.
const auto _GDB_MI_LOG_RE = re.compile(R"re(&"(.*)")re", re.DOTALL);

// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Stream-Records.html#GDB_002fMI-Stream-Records
// "@" string-output
// The target output stream contains any textual output from the
// running target. This is only present when GDB's event loop is truly asynchronous,
// which is currently only the case for remote targets.
const auto _GDB_MI_TARGET_OUTPUT_RE = re.compile(R"re(@"(.*)")re", re.DOTALL);

// Response finished
const auto _GDB_MI_RESPONSE_FINISHED_RE = re.compile(R"(^\(gdb\)\s*$)");

struct Response {
    enum Type_t {
        unknown,
        notify,
        result,
        console,
        log,
        target,
        done,
        output,
        promt
    } type;

    QString message;
    QVariant payload;
    int token = 0;

    Response(Type_t t=unknown, const QString& m={}, const QVariant& p={}, int tok=-1) :
        type{t}, message{m}, payload{p}, token(tok) {}

    bool isValud() const { return type != unknown; }
};

bool response_is_finished(const QString& gdb_mi_text)
{
    // Return true if the gdb mi response is ending
    // Returns: True if gdb response is finished
    return _GDB_MI_RESPONSE_FINISHED_RE.match(gdb_mi_text).hasMatch();
}

namespace priv {

QString::const_iterator& skipspaces(QString::const_iterator& it)
{
    while (it->isSpace())
        ++it;
    return it;
}

QString parseString(const QString& s, QString::const_iterator& it)
{
    QString v;
    while (it != s.cend()) {
        if (*it == '"')
            break;
        if (*it == '\\')
            if (++it == s.cend())
                break;
        v += *it++;
    }
    ++it;
    return v;
}

QString parseKey(const QString& str, QString::const_iterator& it)
{
    QString key;
    while (it != str.cend()) {
        if (*it == '=')
            break;
        if (!it->isSpace())
            key += *it;
        ++it;
    }
    return key;
}

QString consumeTo(QChar c, const QString& str, QString::const_iterator& it)
{
    QString consumed;
    while (it != str.cend()) {
        if (*it == c) {
            ++it;
            break;
        }
        consumed += *it++;
    }
    return consumed;
}

QVariantList parseArray(const QString& str, QString::const_iterator& it);
QVariantMap parseKeyVal(const QString& str, QString::const_iterator& it, QChar terminator='\0');
QVariantMap parseDict(const QString& str, QString::const_iterator& it);
QVariant parseValue(const QString& str, QString::const_iterator& it, QChar terminator);

QVariantList parseArray(const QString& str, QString::const_iterator& it)
{
    QVariantList l;
    while (it != str.cend()) {
        l.append(parseValue(str, it, ']'));
        if (*it == ']') {
            ++it;
            break;
        }
        consumeTo(',', str, it);
    }
    return l;
}

QVariantMap parseDict(const QString& str, QString::const_iterator& it)
{
    QVariantMap m = parseKeyVal(str, it, '}');
    ++it;
    return m;
}

QVariantMap parseKeyVal(const QString& str, QString::const_iterator& it, QChar terminator)
{
    QVariantMap m;
    while (it != str.cend()) {
        auto k = parseKey(str, skipspaces(it));
        auto v = parseValue(str, skipspaces(++it), terminator);
        m.insertMulti(k, v);
        if (*it == terminator) {
            break;
        }
        consumeTo(',', str, it);
    }
    return m;
}

QVariant parseValue(const QString& str, QString::const_iterator& it, QChar terminator)
{
    if (*it == '"') {
        return parseString(str, ++it);
    } else if (*it == '[') {
        return parseArray(str, ++it);
    } else if (*it == '{') {
        return parseDict(str, ++it);
    }
    return parseKeyVal(str, it, terminator);
}

}

QVariantMap parseElements(const QString& str)
{
    auto it = str.cbegin();
    return priv::parseKeyVal(str, it);
}

int strToInt(const QString& s, int def)
{
    bool ok = false;
    int v = s.toInt(&ok);
    return ok? v:def;
}

Response parse_response(const QString& gdb_mi_text)
{
    // Parse gdb mi text and turn it into a dictionary.
    // See https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Stream-Records.html#GDB_002fMI-Stream-Records
    // for details on types of gdb mi output.
    // Args:
    //     gdb_mi_text (str): String output from gdb
    // Returns:
    //    dict with the following keys:
    //    type (either 'notify', 'result', 'console', 'log', 'target', 'done'),
    //    message (str or None),
    //    payload (str, list, dict, or None)
    //
    QRegularExpressionMatch m;
    if ((m = _GDB_MI_NOTIFY_RE.match(gdb_mi_text)).hasMatch()) {
        return { Response::notify, m.captured(2), parseElements(m.captured(3)), strToInt(m.captured(1), -1) };
    } else if ((m = _GDB_MI_RESULT_RE.match(gdb_mi_text)).hasMatch()) {
        return { Response::result, m.captured(2), parseElements(m.captured(3)), strToInt(m.captured(1), -1) };
    } else if ((m = _GDB_MI_CONSOLE_RE.match(gdb_mi_text)).hasMatch()) {
        return { Response::console, m.captured(1), m.captured(0) };
    } else if ((m = _GDB_MI_LOG_RE.match(gdb_mi_text)).hasMatch()) {
        return { Response::log, m.captured(1), m.captured(0) };
    } else if ((m = _GDB_MI_TARGET_OUTPUT_RE.match(gdb_mi_text)).hasMatch()) {
        return { Response::target, m.captured(1), m.captured(0) };
    } else if (_GDB_MI_RESPONSE_FINISHED_RE.match(gdb_mi_text).hasMatch()) {
        return { Response::promt, {}, {}};
    } else {
        // This was not gdb mi output, so it must have just been printed by
        // the inferior program that's being debugged
        return { Response::output, {}, gdb_mi_text };
    }
}

}

struct DebugManager::Priv_t
{
    using responseFunctor_t = std::function<bool (QChar type, const QString&)>;

    int tokenCounter = 0;
    QProcess *gdb;
    QString buffer;
    QHash<int, responseFunctor_t> resposeExpected;

    Priv_t(DebugManager *self) : gdb(new QProcess(self))
    {
    }
};

DebugManager::DebugManager(QObject *parent) :
    QObject(parent),
    self(new Priv_t{this})
{
    qRegisterMetaType<gdb::Frame>();
    qRegisterMetaType<gdb::Breakpoint>();

    setGdbCommand(mi::DEFAULT_GDB_COMMAND);
    connect(self->gdb, &QProcess::readyReadStandardOutput, [this]() {
        for (const auto& c: QString{self->gdb->readAllStandardOutput()})
            switch (c.toLatin1()) {
            case '\r': break;
            case '\n':
                processLine(self->buffer);
                self->buffer.clear();
                break;
            default: self->buffer.append(c); break;
            }
    });
}

DebugManager::~DebugManager()
{
    delete self;
}

DebugManager *DebugManager::instance()
{
    static DebugManager *self = nullptr;
    if (!self)
        self = new DebugManager;
    return self;
}

void DebugManager::execute()
{
    self->gdb->start(QString{"%1 -interpreter=mi"}.arg(gdbCommand()));
}

void DebugManager::quit()
{
    command("-gdb-exit");
}

void DebugManager::command(const QString &cmd)
{
    auto tokStr = QString{"%1"}.arg(self->tokenCounter, 6, 10, QChar{'0'});
    self->tokenCounter = (self->tokenCounter + 1) % 999999;
    self->gdb->write(QString{"%1%2%3"}.arg(tokStr, cmd, mi::EOL).toLocal8Bit());
}

void DebugManager::breakInsert(const QString &path)
{
    command(QString{"-break-insert %1"}.arg(path));
}

void DebugManager::loadExecutable(const QString &file)
{
    command(QString{"-file-exec-and-symbols %1",}.arg(file));
}

void DebugManager::launchLocal()
{
    command("-exec-run");
    m_remote = false;
}

void DebugManager::launchRemote(const QString &remoteTarget)
{
    command(QString{"-target-select remote %1"}.arg(remoteTarget));
    m_remote = true;
}

void DebugManager::commandContinue()
{
    command("-exec-continue");
}

void DebugManager::commandNext()
{
    command("-exec-next");
}

void DebugManager::commandStep()
{
    command("-exec-step");
}

void DebugManager::commandFinish()
{
    command("-exec-finish");
}

void DebugManager::commandInterrupt()
{
    if (isRemote()) {
        command("-exec-interrupt --all");
    } else {
#ifdef Q_OS_UNIX
        ::kill(self->gdb->processId(), SIGINT);
#endif
    }
}

static gdb::Frame parseFrame(const QVariantMap& data)
{
    gdb::Frame f;
    f.addr = data.value("addr").toString().toULongLong(nullptr, 16);
    f.func = data.value("func").toString();
    f.file = data.value("file").toString();
    auto args = data.value("args").toMap();
    for (auto it = args.cbegin(); it != args.cend(); ++it)
        f.params.insert(it.key(), it.value().toString());
    f.fullpath = data.value("fullname").toString();
    f.line = data.value("line").toInt();
    return f;
}

void DebugManager::processLine(const QString &line)
{
    auto r = mi::parse_response(line);

    QTextStream(stdout)
        << "type: " << QMap<mi::Response::Type_t, QString>{
                { mi::Response::unknown, "unknown" },
                { mi::Response::notify, "notify" },
                { mi::Response::result, "result" },
                { mi::Response::console, "console" },
                { mi::Response::log, "log" },
                { mi::Response::target, "target" },
                { mi::Response::done, "done" },
                { mi::Response::output, "output" },
                { mi::Response::promt, "promt" }
               }.value(r.type) << "\n"
        << "message: " << r.message << "\n"
        << "token: " << r.token << "\n"
        << "payload: " << QJsonDocument::fromVariant(r.payload).toJson() << "\n\n";

    switch (r.type) {
    default:
    case mi::Response::unknown:
        break;
    case mi::Response::notify:
        using dispatcher_t = std::function<void(const mi::Response&)>;
        static const dispatcher_t noop = [](const mi::Response&) {};
        static const QMap<QString, dispatcher_t> responseDispatcher{
            { "stopped", [this](const mi::Response& r) {
                auto data = r.payload.toMap();
                QString reason = data.value("reason").toString();
                QString thid = data.value("thread-id").toString();
                int core = data.value("core").toInt();
                gdb::Frame frame = parseFrame(data.value("frame").toMap());
                emit asyncStopped(reason, frame, thid, core);
             } },

             { "running", [this](const mi::Response& r) {
                 auto data = r.payload.toMap();
                 auto thid = data.value("thread-id").toString();
                 emit asyncRunning(thid);
             } },
        };
        responseDispatcher.value(r.message, noop)(r);
        break;
    case mi::Response::result:
        break;
    case mi::Response::console:
        emit streamConsole(mi::escapedText(r.message));
        break;
    case mi::Response::log:
        emit streamGdb(mi::escapedText(r.message));
        break;
    case mi::Response::target:
        emit streamConsole(mi::escapedText(r.message));
        break;
    case mi::Response::done:
        break;
    case mi::Response::output:
        break;
    case mi::Response::promt:
        emit gdbPromnt();
        break;
    }
}
