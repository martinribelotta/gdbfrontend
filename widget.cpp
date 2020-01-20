#include "widget.h"
#include "ui_widget.h"

#include "debugmanager.h"

#include <QTextStream>
#include <QTimer>
#include <QTextCursor>
#include <QTextBlock>

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

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    auto g = DebugManager::instance();
    g->setProperty("state", "notload");
    connect(g, &DebugManager::gdbPromnt, [g]() {
        auto state = g->property("state").toString();
        if (state == "notload") {
            g->loadExecutable("/home/martin/Proyectos/src/dtc/dtc");
            g->setProperty("state", "needbp");
        } else if (state == "needbp") {
            g->breakInsert("main");
            g->setProperty("state", "needrun");
        } else if (state == "needrun") {
            g->launchLocal();
            g->setProperty("state", "");
        }
    });
    connect(g, &DebugManager::asyncRunning, [g, this](const QString& thid) {
        Q_UNUSED(thid)
        ui->textEdit->setEnabled(false);
        ui->buttonRun->setText("stop");
        g->setProperty("executing", true);
    });
    connect(g, &DebugManager::asyncStopped, [this, g](const QString& reason, const gdb::Frame& frame, const QString& thid, int core) {
        Q_UNUSED(reason)
        Q_UNUSED(thid)
        Q_UNUSED(core)
        ui->textEdit->setEnabled(true);
        if (frame.fullpath != ui->textEdit->documentTitle()) {
            QFile f{frame.fullpath};
            if (f.open(QFile::ReadOnly)) {
                QTextStream ss(&f);
                ui->textEdit->setPlainText(ss.readAll());
                ui->textEdit->setDocumentTitle(frame.fullpath);
            }
        }
        highlightLine(ui->textEdit, frame.line);
        ui->textEdit->ensureCursorVisible();
        ui->buttonRun->setText("continue");
        g->setProperty("executing", false);
    });
    g->execute();
    connect(ui->buttonRun, &QPushButton::clicked, [g]() {
        if (g->property("executing").toBool())
            g->commandInterrupt();
        else
            g->commandContinue();
    });
    connect(ui->buttonNext, &QPushButton::clicked, g, &DebugManager::commandNext);
    connect(ui->buttonNextInto, &QPushButton::clicked, g, &DebugManager::commandStep);
    connect(ui->buttonFinish, &QPushButton::clicked, g, &DebugManager::commandFinish);
}

Widget::~Widget()
{
    delete ui;
}

