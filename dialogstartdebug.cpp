#include "dialogstartdebug.h"
#include "ui_dialogstartdebug.h"

#include <QFileDialog>
#include <QTextStream>

DialogStartDebug::DialogStartDebug(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogStartDebug)
{
    m_needWriteInitScript = true;
    ui->setupUi(this);
    connect(ui->buttonChoseExecutable, &QToolButton::clicked, [this]() {
        auto name = QFileDialog::getOpenFileName(this, tr("Select file"));
        if (!name.isEmpty()) {
            ui->editorExecFile->setText(name);
            loadInitScript();
        }
    });
    connect(ui->editorInitScript, &QPlainTextEdit::modificationChanged, [this](bool isModified) {
        if (isModified)
            m_needWriteInitScript = true;
    });
    connect(ui->editorExecFile, &QLineEdit::editingFinished, [this]() {
        if (!ui->editorInitScript->isWindowModified())
            loadInitScript();
    });
}

DialogStartDebug::~DialogStartDebug()
{
    delete ui;
}

QString DialogStartDebug::executableFile() const
{
    return ui->editorExecFile->text();
}

QString DialogStartDebug::initScriptName() const
{
    return m_initScriptName;
}

QString DialogStartDebug::initScript() const
{
    return ui->editorInitScript->toPlainText();
}

QString DialogStartDebug::gdbExecutable() const
{
    return ui->editorGdbExecFile->text();
}

void DialogStartDebug::loadInitScript(const QString &path)
{
    QFileInfo initScript{path};
    if (path.isEmpty()) {
        if (ui->editorExecFile->text().isEmpty())
            return;
        initScript.setFile(QFileInfo{ui->editorExecFile->text()}.absoluteDir().filePath(".gdbinit"));
    }
    if (initScript.exists()) {
        m_initScriptName = initScript.absoluteFilePath();
        QFile f{m_initScriptName};
        if (f.open(QFile::ReadOnly)) {
            QTextStream ss(&f);
            ui->editorInitScript->setPlainText(ss.readAll());
            ui->editorInitScript->setWindowModified(false);
        }
    }
}
