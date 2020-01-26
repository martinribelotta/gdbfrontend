#include "dialogabout.h"
#include "ui_dialogabout.h"

#include <QFile>
#include <QTextBrowser>
#include <QTextStream>

DialogAbout::DialogAbout(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAbout)
{
    ui->setupUi(this);
    QFile gplLicenceFile(":/licences/gpl-3.0.txt");
    gplLicenceFile.open(QFile::ReadOnly);
    QTextStream ss(&gplLicenceFile);
    ui->textBrowser->setPlainText(ss.readAll());
}

DialogAbout::~DialogAbout()
{
    delete ui;
}
