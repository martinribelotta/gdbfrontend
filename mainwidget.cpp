#include "mainwidget.h"
#include "ui_mainwidget.h"

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
#include <QLabel>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

#include <QtDebug>

#include <cmath>

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

template <typename Func1, typename Func2>
static inline QMetaObject::Connection weakConnect(
    typename QtPrivate::FunctionPointer<Func1>::Object *sender, Func1 signal,
    typename QtPrivate::FunctionPointer<Func2>::Object *receiver, Func2 slot)
{

    QMetaObject::Connection conn_normal = QObject::connect(sender, signal, receiver, slot);

    QMetaObject::Connection* conn_delete = new QMetaObject::Connection();

    *conn_delete = QObject::connect(sender, signal, [conn_normal, conn_delete](){
        QObject::disconnect(conn_normal);
        QObject::disconnect(*conn_delete);
        delete conn_delete;
    });
    return conn_normal;
}

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

class ClosableLabel: public QLabel
{
public:
    ClosableLabel(QWidget *parent = nullptr) : QLabel{parent} {
        setAlignment(Qt::AlignHCenter);
        auto em = QFontMetrics(font()).height();
        setMargin(int(0.7 * em));
        setAutoFillBackground(true);
        setStyleSheet("background-color: rgba(255, 0, 0, 0.75)");
        setWordWrap(true);
    }

protected:
    virtual void mousePressEvent(QMouseEvent *) {
        hide();
    }
};

class FileSystemModel: public QFileSystemModel
{
    QTreeView *view;
public:
    FileSystemModel(const QString& root, QTreeView *w) :
        QFileSystemModel(w), view(w)
    {
        setRootPath(root);
        setReadOnly(true);
        view->setModel(this);
        view->setRootIsDecorated(false);
        view->setRootIndex(index(root));
        view->header()->hide();
        for (int i=1; i<columnCount(); i++)
            view->hideColumn(i);
    }

    virtual ~FileSystemModel() {
        view->setModel(nullptr);
    }
};

class StdItemModel: public QStandardItemModel
{
public:
    StdItemModel(const QStringList& labels, QObject *parent = nullptr) :
        QStandardItemModel(parent)
    {
        setHorizontalHeaderLabels(labels);
    }
};

static QLabel *createMessageLabel(QWidget *w)
{
    auto msgLabel = new ClosableLabel(w);
    auto msgLayout = new QVBoxLayout(w);
    auto spacer = new QSpacerItem(100, 1, QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    msgLayout->addWidget(msgLabel);
    msgLayout->setMargin(0);
    msgLayout->addSpacerItem(spacer);
    msgLabel->hide();
    return msgLabel;
}

static void configureEditor(QsciScintilla *ed)
{
    ed->setReadOnly(true);
    ed->setCaretForegroundColor(conf::editor::CARET_FG);
    ed->setCaretLineVisible(true);
    ed->setCaretLineBackgroundColor(conf::editor::CARET_BG);
    ed->setCaretWidth(2);
    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginsForegroundColor(conf::editor::MARGIN_FG);
    ed->setMarginType(1, QsciScintilla::SymbolMargin);
    ed->setMarginWidth(1, "00000");
    ed->setMarginMarkerMask(1, 0x3);
    ed->setMarginSensitivity(1, true);
    ed->markerDefine(QsciScintilla::Circle, QsciScintilla::SC_MARK_CIRCLE);
    ed->setMarkerBackgroundColor(conf::editor::MARKER_CIRCLE_BG, QsciScintilla::SC_MARK_CIRCLE);
    ed->markerDefine(QsciScintilla::Background, QsciScintilla::SC_MARK_BACKGROUND);
    ed->setMarkerBackgroundColor(conf::editor::MARKER_LINE_BG, QsciScintilla::SC_MARK_BACKGROUND);
    ed->setAnnotationDisplay(QsciScintilla::AnnotationIndented);
    ed->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

static void configureSplitters(Ui::MainWidget *ui, QWidget *w)
{
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

#define _tr(text) QApplication::translate(__func__, text)
    ui->stackTraceView->verticalHeader()->hide();
    ui->stackTraceView->horizontalHeader()->setStretchLastSection(true);
    ui->stackTraceView->setModel(new StdItemModel({_tr("Level"), _tr("Function"), _tr("File"), _tr("Line")}, w));

    ui->contextFrameView->verticalHeader()->hide();
    ui->contextFrameView->horizontalHeader()->setStretchLastSection(true);
    ui->contextFrameView->setModel(new StdItemModel({_tr("name"), _tr("value"), _tr("type")}, w));

    ui->watchView->header()->setStretchLastSection(true);
    ui->watchView->setModel(new StdItemModel({_tr("Expression"), _tr("Value"), _tr("Type")}, w));
#undef _tr
}

MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWidget)
{
    ui->setupUi(this);
    configureEditor(ui->textEdit);
    configureSplitters(ui, this);

    auto g = DebugManager::instance();

    connect(ui->buttonAbout, &QToolButton::clicked, []() { DialogAbout().exec(); });
    connect(ui->buttonRun, &QToolButton::clicked, this, &MainWidget::toggleRunStop);
    connect(ui->buttonDebugStart, &QToolButton::clicked, this, &MainWidget::startDebuggin);
    connect(ui->buttonQuit, &QToolButton::clicked, g, &DebugManager::quit);
    connect(ui->buttonNext, &QToolButton::clicked, g, &DebugManager::commandNext);
    connect(ui->buttonNextInto, &QToolButton::clicked, g, &DebugManager::commandStep);
    connect(ui->buttonFinish, &QToolButton::clicked, g, &DebugManager::commandFinish);
    connect(ui->buttonAppQuit, &QToolButton::clicked, this, &MainWidget::close);
    connect(ui->commadLine, &QLineEdit::returnPressed, this, &MainWidget::executeGdbCommand);
    connect(ui->treeView, &QTreeView::activated, this, &MainWidget::fileViewActivate);
    connect(ui->stackTraceView, &QTableView::doubleClicked, this, &MainWidget::stackTraceClicked);
    connect(ui->textEdit, &QsciScintilla::marginClicked, this, &MainWidget::editorMarginClicked);
    connect(ui->buttonWatchAdd, &QToolButton::clicked, this, &MainWidget::buttonAddWatchClicked);
    connect(ui->buttonWatchDel, &QToolButton::clicked, this, &MainWidget::buttonDelWatchClicked);
    connect(ui->buttonWatchClear, &QToolButton::clicked, this, &MainWidget::buttonClrWatchClicked);

    auto msgLabel = createMessageLabel(ui->textEdit);

    connect(g, &DebugManager::gdbError, msgLabel, &QLabel::setText);
    connect(g, &DebugManager::gdbError, msgLabel, &QLabel::show);
    connect(g, &DebugManager::streamDebugInternal, ui->gdbOut, &QTextBrowser::append);
    connect(g, &DebugManager::updateThreads, this, &MainWidget::debugUpdateThreads);
    connect(g, &DebugManager::updateCurrentFrame, this, &MainWidget::debugUpdateCurrentFrame);
    connect(g, &DebugManager::updateLocalVariables, this, &MainWidget::debugUpdateLocalVariables);
    connect(g, &DebugManager::updateStackFrame, this, &MainWidget::debugUpdateStackFrame);
    connect(g, &DebugManager::asyncRunning, this, &MainWidget::debugAsyncRunning);
    connect(g, &DebugManager::asyncStopped, this, &MainWidget::debugAsyncStopped);
    connect(g, &DebugManager::started, this, &MainWidget::updateSourceFiles);
    connect(g, &DebugManager::started, this, &MainWidget::enableGuiItems);
    connect(g, &DebugManager::terminated, this, &MainWidget::disableGuiItems);
    connect(g, &DebugManager::breakpointInserted, this, &MainWidget::debugBreakInserted);
    connect(g, &DebugManager::breakpointRemoved, this, &MainWidget::debugBreakRemoved);
    connect(g, &DebugManager::variableCreated, this, &MainWidget::debugVariableCreated);
    connect(g, &DebugManager::variableDeleted, this, &MainWidget::debugVariableRemoved);
    connect(g, &DebugManager::variablesChanged, this, &MainWidget::debugVariablesUpdate);

    disableGuiItems();
}

MainWidget::~MainWidget()
{
    delete ui;
}

void MainWidget::closeEvent(QCloseEvent *e)
{
    auto g = DebugManager::instance();
    if (g->isGdbExecuting()) {
        auto r = QMessageBox::question(this, tr("Exit?"), tr("Programm is running. Exit anywere?"));
        if (r == QMessageBox::Yes) {
            weakConnect(g, &DebugManager::gdbProcessTerminated, this, &MainWidget::close);
            g->quit();
        }
        e->ignore();
    } else
        e->accept();
}

void MainWidget::executeGdbCommand()
{
    DebugManager::instance()->command(ui->commadLine->text());
}

void MainWidget::ensureTreeViewVisible(const QString &fullpath)
{
    auto model = qobject_cast<QFileSystemModel*>(ui->treeView->model());
    if (model) {
        auto idx = model->index(fullpath);
        if (idx.isValid())
            ui->treeView->setCurrentIndex(idx);
    }
}

void MainWidget::setItemsEnable(bool en)
{
    for (auto& b: ui->buttonGroup->buttons())
        b->setEnabled(en);
    ui->splitterOuter->setEnabled(en);
    ui->buttonDebugStart->setEnabled(!en);
    if (!en) {
        ui->threadSelector->clear();
        ui->textEdit->clear();
        static_cast<QStandardItemModel*>(ui->contextFrameView->model())->clear();
        static_cast<QStandardItemModel*>(ui->contextFrameView->model())->clear();
        static_cast<QStandardItemModel*>(ui->stackTraceView->model())->clear();
        static_cast<QStandardItemModel*>(ui->watchView->model())->clear();
        if (ui->treeView->model()) ui->treeView->model()->deleteLater();
        ui->gdbOut->clear();
    }
}

void MainWidget::updateSourceFiles()
{
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
            new FileSystemModel(commonRoot, ui->treeView);
            DebugManager::instance()->command("-stack-info-frame");
    });
}

bool MainWidget::openFile(const QString &fullpath)
{
    QFile f{fullpath};
    if (!f.open(QFile::ReadOnly)) {
        qDebug() << "error opening file " << fullpath << f.errorString();
        return false;
    }
    ui->textEdit->read(&f);
    int chars = ::log10(ui->textEdit->lines()) + 1;
    int w = QFontMetrics(ui->textEdit->font()).width("00") * chars;
    ui->textEdit->setMarginWidth(0, w);
    ui->textEdit->setWindowFilePath(fullpath);
    ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_BACKGROUND);
    ui->textEdit->markerDeleteAll(QsciScintilla::SC_MARK_CIRCLE);
    auto bpList = DebugManager::instance()->breakpointsForFile(fullpath);
    for (const auto& bp: bpList)
        ui->textEdit->markerAdd(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
    ensureTreeViewVisible(fullpath);
    return true;
}

void MainWidget::toggleBreakpointAt(const QString &file, int line)
{
    auto g = DebugManager::instance();
    auto bp = g->breakpointByFileLine(file, line);
    if (!bp.isValid()) {
        auto bpText = QString{"%1:%2"}.arg(file).arg(line);
        g->breakInsert(bpText);
    } else {
        g->breakRemove(bp.number);
    }
}

void MainWidget::buttonAddWatchClicked() {
    DialogNewWatch d(ui->textEdit->selectedText(), this);
    if (d.exec())
        DebugManager::instance()->traceAddVariable(d.watchExpr(), d.watchName());
}

void MainWidget::buttonDelWatchClicked()
{
    auto watchModel = static_cast<QStandardItemModel*>(ui->watchView->model());
    for (const auto i: ui->watchView->selectionModel()->selectedRows(0)) {
        auto item = watchModel->itemFromIndex(i);
        if (item)
            DebugManager::instance()->traceDelVariable(item->text());
    }
}

void MainWidget::buttonClrWatchClicked() {
    auto watchModel = static_cast<QStandardItemModel*>(ui->watchView->model());
    auto g = DebugManager::instance();
    for (int row=0; row<watchModel->rowCount(); row++) {
        auto item = watchModel->item(row, 0);
        if (item)
            g->traceDelVariable(item->text());
    }
}

void MainWidget::editorMarginClicked(int margin, int line, Qt::KeyboardModifiers) {
    if (margin == 1)
        toggleBreakpointAt(ui->textEdit->windowFilePath(), line+1);
}

void MainWidget::fileViewActivate(const QModelIndex &idx) {
    auto model = qobject_cast<QFileSystemModel*>(ui->treeView->model());
    if (model) {
        auto info = model->fileInfo(idx);
        if (info.isFile())
            openFile(info.absoluteFilePath());
    }
}

void MainWidget::stackTraceClicked(const QModelIndex &idx) {
    auto m = qobject_cast<QStandardItemModel*>(ui->stackTraceView->model());
    if (m) {
        auto item = m->item(idx.row(), 0);
        if (item) {
            auto frame = item->data().value<gdb::Frame>();
            DebugManager::instance()->commandAndResponse(
                        QString{"-stack-select-frame %1"}.arg(frame.level),
                        [this](const QVariant&) { triggerUpdateContext(); });
        } else
            qDebug() << "not item for model" << idx;
    } else
        qDebug() << "not model for stack trace view";
}

void MainWidget::startDebuggin()
{
    DialogStartDebug d{this};
    if (d.exec() == QDialog::Accepted) {
        auto g = DebugManager::instance();
        QStringList argv;
        if (d.needWriteInitScript()) {
            auto gdbinit = new QTemporaryFile{QDir{QDir::tempPath()}.filePath(".gdbinit-XXXXXX"), this};
            connect(g, &DebugManager::terminated, [gdbinit]() { gdbinit->remove(); });
            if (gdbinit->open()) {
                gdbinit->write(d.initScript().toLocal8Bit());
                gdbinit->close();
                argv.append({ "-x", gdbinit->fileName() });
            }
        } else {
            argv.append({ "-x", d.initScriptName() });
        }
        argv.append(d.executableFile());
        g->setGdbArgs(argv);
        g->setGdbCommand(d.gdbExecutable());
        g->execute();
    }
}

void MainWidget::triggerUpdateContext()
{
    auto g = DebugManager::instance();
    g->command("-stack-info-frame");
    g->command("-thread-info");
    g->command("-stack-list-frames");
    g->command("-stack-list-variables --simple-values");
}

void MainWidget::toggleRunStop()
{
    auto g = DebugManager::instance();
    if (g->isInferiorRunning())
        g->commandInterrupt();
    else
        g->commandContinue();
}

void MainWidget::debugUpdateLocalVariables(const QList<gdb::Variable> &locals) {
    auto model = static_cast<QStandardItemModel*>(ui->contextFrameView->model());
    model->clear();
    for (const auto& e: locals) {
        model->appendRow({
                             new QStandardItem{e.name},
                             new QStandardItem{e.value},
                             new QStandardItem{e.type},
                         });
    }
    ui->contextFrameView->resizeColumnToContents(0);
}

void MainWidget::debugUpdateCurrentFrame(const gdb::Frame &frame) {
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
}

void MainWidget::debugUpdateThreads(int curr, const QList<gdb::Thread> &threads)
{
    ui->threadSelector->clear();
    int currIdx = -1;
    for (const auto& e: threads) {
        ui->threadSelector->addItem(e.targetId);
        if (curr == e.id)
            currIdx = ui->threadSelector->count() - 1;
    }
    if (currIdx != -1)
        ui->threadSelector->setCurrentIndex(currIdx);
}

void MainWidget::debugUpdateStackFrame(const QList<gdb::Frame> &stackTrace)
{
    auto model = static_cast<QStandardItemModel*>(ui->stackTraceView->model());
    model->clear();
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
}

void MainWidget::debugAsyncStopped(const gdb::AsyncContext& ctx)
{
    if (ctx.reason == gdb::AsyncContext::Reason::exitedNormally) {
        DebugManager::instance()->quit();
    } else {
        ui->buttonRun->setIcon(QIcon{":/images/debug-run-v2.svg"});
        triggerUpdateContext();
        DebugManager::instance()->traceUpdateAll();
    }
}

void MainWidget::debugAsyncRunning()
{
    ui->buttonRun->setIcon(QIcon{":/images/debug-pause-v2.svg"});
}

void MainWidget::debugBreakInserted(const gdb::Breakpoint &bp) {
    if (bp.fullname == ui->textEdit->windowFilePath())
        ui->textEdit->markerAdd(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
}

void MainWidget::debugBreakRemoved(const gdb::Breakpoint &bp) {
    if (bp.fullname == ui->textEdit->windowFilePath())
        ui->textEdit->markerDelete(bp.line - 1, QsciScintilla::SC_MARK_CIRCLE);
}

void MainWidget::debugVariableCreated(const gdb::Variable &var) {
    static_cast<QStandardItemModel*>(ui->watchView->model())->appendRow({
        new QStandardItem{var.name},
        new QStandardItem{var.value},
        new QStandardItem{var.type}
    });
    ui->watchView->header()->resizeSections(QHeaderView::ResizeToContents);
}

void MainWidget::debugVariableRemoved(const gdb::Variable &var) {
    auto watchModel = static_cast<QStandardItemModel*>(ui->watchView->model());
    bool removeMore = true;
    while (removeMore) {
        auto items = watchModel->findItems(var.name);
        removeMore = !items.empty();
        if (removeMore)
            watchModel->removeRow(items.first()->row());
    }
    ui->watchView->header()->resizeSections(QHeaderView::ResizeToContents);
}

void MainWidget::debugVariablesUpdate(const QStringList &changes) {
    auto watchModel = static_cast<QStandardItemModel*>(ui->watchView->model());
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
}
