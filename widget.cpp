#include "widget.h"
#include "ui_widget.h"

#include "debugmanager.h"
#include "dialogabout.h"
#include "dialognewwatch.h"
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
#include <QDir>
#include <QFileSystemModel>

#include <QtDebug>

namespace conf {

namespace editor {

const auto CARET_FG = QColor("#ff0000ff");
const auto CARET_BG = QColor("#1f0000ff");
const auto MARGIN_FG = QColor("#ff888888");
const auto MARKER_CIRCLE_BG = QColor("#ee1111");
const auto MARKER_LINE_BG = QColor("#eeee11");

}

}

static const QRegularExpression UNWANTED_PATH{R"(/(usr|opt|lib|arm-none-eabi)/.*)"};

static QString find_root(const QStringList& list)
{
    QString root = list.front();
    for(const auto& item : list) {
        if (root.length() > item.length())
            root.truncate(item.length());
        for(int i = 0; i < root.length(); ++i)
            if (root[i] != item[i]) {
                root.truncate(i);
                break;
            }
    }
    return root;
}

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    auto ed = ui->textEdit;

    ed->setReadOnly(true);

    ed->setCaretForegroundColor(conf::editor::CARET_FG);
    ed->setCaretLineVisible(true);
    ed->setCaretLineBackgroundColor(conf::editor::CARET_BG);
    ed->setCaretWidth(2);

    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginsForegroundColor(conf::editor::MARGIN_FG);

    ed->setMarginType(1, QsciScintilla::SymbolMargin);
    ed->setMarginWidth(1, "00000");
    ed->setMarginMarkerMask(1, 0xF);
    ed->setMarginSensitivity(1, true);

    ed->markerDefine(QsciScintilla::Circle, QsciScintilla::SC_MARK_CIRCLE);
    ed->setMarkerBackgroundColor(conf::editor::MARKER_CIRCLE_BG, QsciScintilla::SC_MARK_CIRCLE);

    ed->markerDefine(QsciScintilla::Background, QsciScintilla::SC_MARK_BACKGROUND);
    ed->setMarkerBackgroundColor(conf::editor::MARKER_LINE_BG, QsciScintilla::SC_MARK_BACKGROUND);

    ed->setAnnotationDisplay(QsciScintilla::AnnotationIndented);

    ed->setFont(QFont{"Monospace, Consolas, Courier", QFont{}.pointSize()});

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
    auto ensureTreeViewVisible = [this](const QString& fullpath) {
        auto model = qobject_cast<QFileSystemModel*>(ui->treeView->model());
        if (model) {
            auto idx = model->index(fullpath);
            if (idx.isValid())
                ui->treeView->setCurrentIndex(idx);
        }
    };
    auto openFile = [this, ensureTreeViewVisible](const QString& fullpath) -> bool {
        QFile f{fullpath};
        if (!f.open(QFile::ReadOnly)) {
            qDebug() << "error opening file " << fullpath << f.errorString();
            return false;
        }
        ui->textEdit->read(&f);
        ui->textEdit->setWindowFilePath(fullpath);
        ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_BACKGROUND);
        ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_CIRCLE);
        auto bpList = DebugManager::instance()->breakpointsForFile(fullpath);
        for (const auto& bp: bpList)
            ui->textEdit->markerAdd(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
        ensureTreeViewVisible(fullpath);
        return true;
    };
    connect(ui->treeView, &QTreeView::activated, [this, openFile](const QModelIndex& idx) {
        auto model = qobject_cast<QFileSystemModel*>(ui->treeView->model());
        if (!model)
            return;
        auto info = model->fileInfo(idx);
        if (info.isFile())
            openFile(info.absoluteFilePath());
    });
    connect(g, &DebugManager::updateCurrentFrame, [this, openFile, ensureTreeViewVisible](const gdb::Frame& frame) {
        if (frame.fullpath != ui->textEdit->windowFilePath()) {
            if (!openFile(frame.fullpath))
                return;
        } else {
            ensureTreeViewVisible(frame.fullpath);
        }
        int line = frame.line - 1;
        ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_BACKGROUND);
        ui->textEdit->markerAdd(line, QsciScintilla::SC_MARK_BACKGROUND);
        ui->textEdit->ensureLineVisible(line);
    });
    connect(ed, &QsciScintilla::marginClicked, [this](int margin, int line, Qt::KeyboardModifiers) {
        if (margin == 1) {
            auto g = DebugManager::instance();
            auto fullname = ui->textEdit->windowFilePath();
            line++;
            auto bp = g->breakpointByFileLine(fullname, line);
            if (!bp.isValid()) {
                auto bpText = QString{"%1:%2"}.arg(fullname).arg(line);
                g->breakInsert(bpText);
            } else {
                g->breakRemove(bp.number);
            }
        }
    });
    connect(g, &DebugManager::breakpointInserted, [this](const gdb::Breakpoint& bp) {
        if (bp.fullname == ui->textEdit->windowFilePath())
            ui->textEdit->markerAdd(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
    });
    connect(g, &DebugManager::breakpointRemoved, [this](const gdb::Breakpoint& bp) {
        if (bp.fullname == ui->textEdit->windowFilePath())
            ui->textEdit->markerDelete(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
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
        g->traceUpdateAll();
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
            if (ui->treeView->model())
                ui->treeView->model()->deleteLater();
            ui->gdbOut->clear();
        }
    };
    auto updateSourceFiles = [this]() {
        DebugManager::instance()->commandAndResponse(
            "-file-list-exec-source-files", [this](const QVariant& res) {
                auto fileListData = res.toMap().value("files").toList();
                QSet<QString> files;
                for (const auto& e: fileListData) {
                    auto v = e.toMap();
                    auto info = QFileInfo{v.value("fullname").toString()};
                    if (info.exists() && !UNWANTED_PATH.match(info.absoluteFilePath()).hasMatch()) {
                        files.insert(info.absoluteFilePath());
                    }
                }
                auto fileList = files.toList();
                auto commonRoot = find_root(fileList);
                auto model = new QFileSystemModel(this);
                model->setReadOnly(true);
                model->setRootPath(commonRoot);
                if (ui->treeView->model())
                    ui->treeView->model()->deleteLater();
                ui->treeView->setModel(model);
                ui->treeView->setRootIndex(model->index(commonRoot));
                ui->treeView->setRootIsDecorated(false);
                for (int i=1; i<model->columnCount(); i++)
                    ui->treeView->hideColumn(i);
                DebugManager::instance()->command("-stack-info-frame");
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
    setProperty("aboutToQuit", false);
    connect(g, &DebugManager::gdbProcessTerminated, [this]() {
        if (property("aboutToQuit").toBool())
            close();
    });
    connect(ui->buttonAppQuit, &QToolButton::clicked, [this]() {
        auto g = DebugManager::instance();
        if (g->isGdbExecuting()) {
            setProperty("aboutToQuit", true);
            g->quit();
        } else
            close();
    });

    auto watchModel = new QStandardItemModel(this);
    watchModel->setHorizontalHeaderLabels({ tr("Expression"), tr("Value"), tr("Type") });
    ui->watchView->header()->setStretchLastSection(true);
    ui->watchView->setModel(watchModel);
    connect(ui->buttonWatchAdd, &QToolButton::clicked, [this]() {
        DialogNewWatch d(ui->textEdit->selectedText(), this);
        if (d.exec())
            DebugManager::instance()->traceAddVariable(d.watchExpr(), d.watchName());
    });
    connect(ui->buttonWatchDel, &QToolButton::clicked, [this, watchModel]() {
        auto itemsSelected = ui->watchView->selectionModel()->selectedRows(0);
        for (const auto i: itemsSelected) {
            auto item = watchModel->itemFromIndex(i);
            if (item) {
                auto name = item->text();
                DebugManager::instance()->traceDelVariable(name);
            }
        }
    });
    connect(ui->buttonWatchClear, &QToolButton::clicked, [this, watchModel]() {
        auto g = DebugManager::instance();
        for (int row=0; row<watchModel->rowCount(); row++) {
            auto item = watchModel->item(row, 0);
            if (item) {
                auto name = item->text();
                g->traceDelVariable(name);
            }
        }
    });
    connect(g, &DebugManager::variableCreated, [this, watchModel](const gdb::Variable& var) {
        watchModel->appendRow({
            new QStandardItem{var.name},
            new QStandardItem{var.value},
            new QStandardItem{var.type}
        });
        ui->watchView->header()->resizeSections(QHeaderView::ResizeToContents);
    });
    connect(g, &DebugManager::variableDeleted, [this, watchModel](const gdb::Variable& var) {
        bool removeMore = true;
        while (removeMore) {
            auto items = watchModel->findItems(var.name);
            removeMore = !items.empty();
            if (removeMore)
                watchModel->removeRow(items.first()->row());
        }
        ui->watchView->header()->resizeSections(QHeaderView::ResizeToContents);
    });
    connect(g, &DebugManager::variablesChanged, [this, watchModel](const QStringList& changes) {
        QList<int> rowsChanged;
        for (const auto& e: changes)
            for (const auto& k: watchModel->findItems(e))
                rowsChanged += k->row();
        auto g = DebugManager::instance();
        for (const auto& row: rowsChanged) {
            auto name = watchModel->item(row, 0)->text();
            auto item = watchModel->item(row, 1);
            auto var = g->vatchVars().value(name);
            item->setText(var.value);
        }
        ui->watchView->header()->resizeSections(QHeaderView::ResizeToContents);
    });

    setEnable(false);
}

Widget::~Widget()
{
    delete ui;
}

