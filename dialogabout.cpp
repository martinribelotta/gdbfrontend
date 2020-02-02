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
    if (gplLicenceFile.open(QFile::ReadOnly))
        ui->textBrowser->setPlainText(QTextStream(&gplLicenceFile).readAll());
}

DialogAbout::~DialogAbout()
{
    delete ui;
}
