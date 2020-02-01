#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

#include "debugmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
private:
    Ui::Widget *ui;

protected:
    virtual void closeEvent(QCloseEvent *e);

private slots:
    void ensureTreeViewVisible(const QString& fullpath);
    void setItemsEnable(bool en);
    void enableGuiItems() { setItemsEnable(true); }
    void disableGuiItems() { setItemsEnable(false); }
    void updateSourceFiles();
    bool openFile(const QString& fullpath);
    void toggleBreakpointAt(const QString& file, int line);

    void startDebuggin();
    void triggerUpdateContext();
    void toggleRunStop();
};
#endif // WIDGET_H
