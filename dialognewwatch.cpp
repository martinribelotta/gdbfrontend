#include "dialognewwatch.h"
#include "ui_dialognewwatch.h"

DialogNewWatch::DialogNewWatch(const QString &expr, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogNewWatch)
{
    ui->setupUi(this);
    ui->editorExpression->setText(expr);
}

DialogNewWatch::~DialogNewWatch()
{
    delete ui;
}

QString DialogNewWatch::watchName() const
{
    return ui->checkUseAutoName->isChecked()? ui->editorExpression->text() : ui->editorName->text();
}

QString DialogNewWatch::watchExpr() const
{
    return ui->editorExpression->text();
}

void DialogNewWatch::setWatchName(QString watchName)
{
    ui->editorName->setText(watchName);
    ui->checkUseAutoName->setChecked(false);
}

void DialogNewWatch::setWatchExpr(QString watchExpr)
{
    ui->editorExpression->setText(watchExpr);
}
