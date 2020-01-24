#include "widget.h"
#include "ui_widget.h"

#include "debugmanager.h"

#include <QTextStream>
#include <QTimer>
#include <QTextCursor>
#include <QTextBlock>
#include <QStandardItemModel>

#include <QtDebug>

void highlightLine(QTextEdit *b, int line)
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    QTextEdit::ExtraSelection selection;

    QColor lineColor = QColor(Qt::green).lighter(160);

    selection.format.setBackground(lineColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = QTextCursor{b->document()->findBlockByNumber(line - 1)};
    selection.cursor.clearSelection();
    extraSelections.append(selection);

    b->setExtraSelections(extraSelections);
    b->setTextCursor(selection.cursor);
}

static QString threadToText(const QVariantMap& t)
{
    auto name = t.value("name").toString();
    auto id = t.value("id").toString();
    auto state = t.value("state").toString();
    auto core = t.value("core").toString();
    return QString{"Thread: %1, id: %2, state: %3, core: %4"}.arg(name, id, state, core);
}

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    ui->splitter_1->setStretchFactor(0, 1);
    ui->splitter_1->setStretchFactor(1, 0);
    ui->splitter_2->setStretchFactor(0, 1);
    ui->splitter_2->setStretchFactor(1, 0);
    ui->splitter_3->setStretchFactor(0, 1);
    ui->splitter_3->setStretchFactor(1, 0);
    ui->splitter_4->setStretchFactor(0, 1);
    ui->splitter_4->setStretchFactor(1, 0);
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
    connect(g, &DebugManager::gdbPromt, [this]() {
        auto g = DebugManager::instance();
        auto state = property("state").toString();
        if (state == "notload") {
            g->loadExecutable("/home/martin/Proyectos/src/dtc/dtc");
            setProperty("state", "needbp");
        } else if (state == "needbp") {
            g->breakInsert("main");
            setProperty("state", "needrun");
        } else if (state == "needrun") {
            g->launchLocal();
            setProperty("state", "");
        }
    });
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
    connect(g, &DebugManager::updateCurrentFrame, [this](const gdb::Frame& frame) {
        if (frame.fullpath != ui->textEdit->documentTitle()) {
            QFile f{frame.fullpath};
            if (!f.open(QFile::ReadOnly)) {
                qDebug() << "error opening file " << frame.fullpath << f.errorString();
                return;
            }
            QTextStream ss(&f);
            ui->textEdit->setPlainText(ss.readAll());
            ui->textEdit->setDocumentTitle(frame.fullpath);
        }
        highlightLine(ui->textEdit, frame.line);
        ui->textEdit->ensureCursorVisible();
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
        ui->buttonRun->setText("stop");
        setProperty("executing", true);
    });
    connect(g, &DebugManager::asyncStopped, [this](const QString& reason, const gdb::Frame& frame, const QString& thid, int core) {
        Q_UNUSED(reason)
        Q_UNUSED(thid)
        Q_UNUSED(core)
        Q_UNUSED(frame)
        ui->textEdit->setEnabled(true);
        ui->buttonRun->setText("continue");
        setProperty("executing", false);

        auto g = DebugManager::instance();
        g->command("-stack-info-frame");
        g->command("-thread-info");
        g->command("-stack-list-frames");
        g->command("-stack-list-variables --simple-values");
    });
    connect(ui->buttonRun, &QPushButton::clicked, [this]() {
        auto g = DebugManager::instance();
        if (property("executing").toBool())
            g->commandInterrupt();
        else
            g->commandContinue();
    });
    connect(ui->buttonNext, &QPushButton::clicked, g, &DebugManager::commandNext);
    connect(ui->buttonNextInto, &QPushButton::clicked, g, &DebugManager::commandStep);
    connect(ui->buttonFinish, &QPushButton::clicked, g, &DebugManager::commandFinish);
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

    g->execute();
}

Widget::~Widget()
{
    delete ui;
}

