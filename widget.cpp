#include "widget.h"
#include "ui_widget.h"

#include "debugmanager.h"
#include "dialogabout.h"
#include "dialogstartdebug.h"

#include <Qsci/qsciscintilla.h>

#include <QTextStream>
#include <QTimer>
#include <QTextCursor>
#include <QTextBlock>
#include <QStandardItemModel>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QMessageBox>

#include <QtDebug>

static const QRegularExpression UNWANTED_PATH{R"(/(usr|opt|lib)/.*)"};

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    auto ed = ui->textEdit;

    ed->setReadOnly(true);

    ed->setCaretForegroundColor(QColor("#ff0000ff"));
    ed->setCaretLineVisible(true);
    ed->setCaretLineBackgroundColor(QColor("#1f0000ff"));
    ed->setCaretWidth(2);

    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginsForegroundColor(QColor("#ff888888"));

    ed->setMarginType(1, QsciScintilla::SymbolMargin);
    ed->setMarginWidth(1, "00000");
    ed->setMarginMarkerMask(1, 0xF);
    ed->setMarginSensitivity(1, true);

    ed->markerDefine(QsciScintilla::Circle, QsciScintilla::SC_MARK_CIRCLE);
    ed->setMarkerBackgroundColor(QColor("#ee1111"), QsciScintilla::SC_MARK_CIRCLE);

    ed->markerDefine(QsciScintilla::Background, QsciScintilla::SC_MARK_BACKGROUND);
    ed->setMarkerBackgroundColor(QColor("#eeee11"), QsciScintilla::SC_MARK_BACKGROUND);

    ed->setAnnotationDisplay(QsciScintilla::AnnotationIndented);

    ed->setFont(QFont{"Monospace, Consolas, Courier"});

    ui->splitterOuter->setStretchFactor(0, 0);
    ui->splitterOuter->setStretchFactor(1, 1);

    ui->splitterCodeEditor->setStretchFactor(0, 1);
    ui->splitterCodeEditor->setStretchFactor(1, 0);

    ui->splitterUpper->setStretchFactor(0, 1);
    ui->splitterUpper->setStretchFactor(1, 0);

    ui->splitterBottom->setStretchFactor(0, 1);
    ui->splitterBottom->setStretchFactor(1, 0);

    ui->splitterInner->setStretchFactor(0, 1);
    ui->splitterInner->setStretchFactor(1, 0);

    ui->contextFrameView->verticalHeader()->hide();
    ui->contextFrameView->horizontalHeader()->setStretchLastSection(true);
    ui->stackTraceView->verticalHeader()->hide();
    ui->stackTraceView->horizontalHeader()->setStretchLastSection(true);

    auto g = DebugManager::instance();
    setProperty("state", "notload");
    connect(ui->commadLine, &QLineEdit::returnPressed, [this]() {
        DebugManager::instance()->command(ui->commadLine->text());
    });
    connect(g, &DebugManager::streamDebugInternal, ui->gdbOut, &QTextBrowser::append);
    connect(g, &DebugManager::updateThreads, [this](int curr, const QList<gdb::Thread>& threads) {
        ui->threadSelector->clear();
        int currIdx = -1;
        for (const auto& e: threads) {
            ui->threadSelector->addItem(e.targetId);
            if (curr == e.id)
                currIdx = ui->threadSelector->count() - 1;
        }
        if (currIdx != -1)
            ui->threadSelector->setCurrentIndex(currIdx);
    });
    auto openFile = [this](const QString& fullpath) -> bool {
        QFile f{fullpath};
        if (!f.open(QFile::ReadOnly)) {
            qDebug() << "error opening file " << fullpath << f.errorString();
            return false;
        }
        ui->textEdit->read(&f);
        ui->textEdit->setWindowFilePath(fullpath);
        ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_BACKGROUND);
        ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_CIRCLE);
        auto model = qobject_cast<QStandardItemModel*>(ui->listView->model());
        if (model) {
            for (int i=0; i<model->rowCount(); i++) {
                auto item = model->item(i);
                if (item->data().toString() == fullpath) {
                    ui->listView->setCurrentIndex(model->indexFromItem(item));
                }
            }
        }
        auto bpList = DebugManager::instance()->breakpointsForFile(fullpath);
        for (const auto& bp: bpList)
            ui->textEdit->markerAdd(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
        return true;
    };
    connect(ui->listView, &QListView::activated, [this, openFile](const QModelIndex& idx) {
        auto model = qobject_cast<QStandardItemModel*>(ui->listView->model());
        if (!model)
            return;
        auto item = model->itemFromIndex(idx);
        if (!item)
            return;
        auto fullpath = item->data().toString();
        openFile(fullpath);
    });
    connect(g, &DebugManager::updateCurrentFrame, [this, openFile](const gdb::Frame& frame) {
        if (frame.fullpath != ui->textEdit->windowFilePath())
            if (!openFile(frame.fullpath))
                return;
        int line = frame.line - 1;
        ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_BACKGROUND);
        ui->textEdit->markerAdd(line, QsciScintilla::SC_MARK_BACKGROUND);
        ui->textEdit->ensureLineVisible(line);
    });
    connect(ed, &QsciScintilla::marginClicked, [this](int margin, int line, Qt::KeyboardModifiers) {
        if (margin == 1) {
            auto g = DebugManager::instance();
            auto fullname = ui->textEdit->windowFilePath();
            auto haveMarker = (ui->textEdit->markersAtLine(line) & (1 << QsciScintilla::SC_MARK_CIRCLE)) != 0;
            if (!haveMarker) {
                auto bpText = QString{"%1:%2"}.arg(fullname).arg(line);
                g->breakInsert(bpText);
            } else {
                auto bp = g->breakpointByFileLine(fullname, line);
                if (bp.isValid())
                    g->breakRemove(bp.number);
            }
        }
    });
    connect(g, &DebugManager::breakpointInserted, [this](const gdb::Breakpoint& bp) {
        if (bp.fullname == ui->textEdit->windowFilePath())
            ui->textEdit->markerAdd(bp.line, QsciScintilla::SC_MARK_CIRCLE);
    });
    connect(g, &DebugManager::breakpointRemoved, [this](const gdb::Breakpoint& bp) {
        if (bp.fullname == ui->textEdit->windowFilePath())
            ui->textEdit->markerDelete(bp.line, QsciScintilla::SC_MARK_CIRCLE);
    });
    connect(g, &DebugManager::updateLocalVariables, [this](const QList<gdb::Variable>& locals) {
        auto model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({ tr("name"), tr("value"), tr("type") });
        if (ui->contextFrameView->model())
            ui->contextFrameView->model()->deleteLater();
        ui->contextFrameView->setModel(model);
        for (const auto& e: locals) {
            model->appendRow({
                new QStandardItem{e.name},
                new QStandardItem{e.value},
                new QStandardItem{e.type},
            });
        }
        ui->contextFrameView->resizeColumnToContents(0);

    });
    connect(g, &DebugManager::updateStackFrame, [this](const QList<gdb::Frame>& stackTrace) {
        auto model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({ tr("Level"), tr("Function"), tr("File"), tr("Line") });
        if (ui->stackTraceView->model())
            ui->stackTraceView->model()->deleteLater();
        ui->stackTraceView->setModel(model);
        for (const auto& frame: stackTrace) {
            QStandardItem *first;
            model->appendRow({
                first = new QStandardItem{QString("%1").arg(frame.level)},
                new QStandardItem{frame.func.isEmpty()? QString{"0x%1"}.arg(frame.addr, '0', 16) : frame.func},
                new QStandardItem{frame.file},
                new QStandardItem{QString("%1").arg(frame.line)},
                });
            first->setData(QVariant::fromValue(frame));
        }
        for (int i = 0; i<ui->stackTraceView->horizontalHeader()->count() - 1; i++)
            ui->stackTraceView->resizeColumnToContents(i);
        ui->stackTraceView->resizeRowsToContents();
    });
    connect(g, &DebugManager::asyncRunning, [this](const QString& thid) {
        Q_UNUSED(thid)
        ui->textEdit->setEnabled(false);
        ui->buttonRun->setIcon(QIcon{":/images/debug-pause-v2.svg"});
        setProperty("executing", true);
    });
    connect(g, &DebugManager::asyncStopped, [this](const QString& reason, const gdb::Frame& frame, const QString& thid, int core) {
        Q_UNUSED(reason)
        Q_UNUSED(thid)
        Q_UNUSED(core)
        Q_UNUSED(frame)
        ui->textEdit->setEnabled(true);
        ui->buttonRun->setIcon(QIcon{":/images/debug-run-v2.svg"});
        setProperty("executing", false);

        auto g = DebugManager::instance();
        g->command("-stack-info-frame");
        g->command("-thread-info");
        g->command("-stack-list-frames");
        g->command("-stack-list-variables --simple-values");
    });
    connect(ui->buttonRun, &QToolButton::clicked, [this]() {
        auto g = DebugManager::instance();
        if (property("executing").toBool())
            g->commandInterrupt();
        else
            g->commandContinue();
    });
    connect(ui->buttonDebugStart, &QToolButton::clicked, [this]() {
        DialogStartDebug d{this};
        if (d.exec() == QDialog::Accepted) {
            QStringList argv;
            if (d.needWriteInitScript()) {
                auto gdbinit = new QTemporaryFile{".gdbinit-XXXXXX", this};
                if (gdbinit->open()) {
                    gdbinit->write(d.initScript().toLocal8Bit());
                    gdbinit->close();
                    argv.append({ "-x", gdbinit->fileName() });
                }
            } else {
                argv.append({ "-x", d.initScriptName() });
            }
            argv.append(d.executableFile());
            auto g = DebugManager::instance();
            g->setGdbArgs(argv);
            g->setGdbCommand(d.gdbExecutable());
            g->execute();
        }
    });
    auto setEnable = [this](bool en) {
        for (auto& b: ui->buttonGroup->buttons())
            b->setEnabled(en);
        ui->splitterOuter->setEnabled(en);
        ui->buttonDebugStart->setEnabled(!en);
        if (!en) {
            ui->threadSelector->clear();
            ui->textEdit->clear();
            if (ui->contextFrameView->model())
                ui->contextFrameView->model()->deleteLater();
            if (ui->stackTraceView->model())
                ui->stackTraceView->model()->deleteLater();
            if (ui->listView->model())
                ui->listView->model()->deleteLater();
            ui->gdbOut->clear();
        }
    };
    auto updateSourceFiles = [this]() {
        DebugManager::instance()->commandAndResponse(
            "-file-list-exec-source-files", [this](const QVariant& res) {
                auto fileList = res.toMap().value("files").toList();
                auto model = new QStandardItemModel(this);
                if (ui->listView->model())
                    ui->listView->model()->deleteLater();
                ui->listView->setModel(model);
                QSet<QString> files;
                for (const auto& e: fileList) {
                    auto v = e.toMap();
                    auto info = QFileInfo{v.value("fullname").toString()};
                    if (info.exists() && !UNWANTED_PATH.match(info.absoluteFilePath()).hasMatch()) {
                        files.insert(info.absoluteFilePath());
                    }
                }
                for (const auto& e: files) {
                    auto info = QFileInfo{e};
                    auto item = new QStandardItem{info.fileName()};
                    item->setData(info.absoluteFilePath());
                    model->appendRow(item);
                }
            });
    };
    connect(g, &DebugManager::started, [setEnable, updateSourceFiles]() {
        updateSourceFiles();
        setEnable(true);
    });
    connect(g, &DebugManager::terminated, [setEnable]() { setEnable(false); });
    connect(ui->buttonQuit, &QToolButton::clicked, g, &DebugManager::quit);
    connect(ui->buttonNext, &QToolButton::clicked, g, &DebugManager::commandNext);
    connect(ui->buttonNextInto, &QToolButton::clicked, g, &DebugManager::commandStep);
    connect(ui->buttonFinish, &QToolButton::clicked, g, &DebugManager::commandFinish);
    connect(ui->stackTraceView, &QTableView::doubleClicked, [this](const QModelIndex& idx) {
        auto m = qobject_cast<QStandardItemModel*>(ui->stackTraceView->model());
        if (m) {
            auto item = m->item(idx.row(), 0);
            if (item) {
                auto frame = item->data().value<gdb::Frame>();
                auto g = DebugManager::instance();
                g->commandAndResponse(QString{"-stack-select-frame %1"}.arg(frame.level), [](const QVariant&) {
                    auto g = DebugManager::instance();
                    g->command("-stack-info-frame");
                    g->command("-thread-info");
                    g->command("-stack-list-frames");
                    g->command("-stack-list-variables --simple-values");
                });
            } else
                qDebug() << "not item for model" << idx;
        } else
            qDebug() << "not model for stack trace view";
    });
    connect(ui->buttonAbout, &QToolButton::clicked, []() { DialogAbout().exec(); });

    setEnable(false);
}

Widget::~Widget()
{
    delete ui;
}

