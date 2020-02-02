#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

#include "debugmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWidget; }
QT_END_NAMESPACE

class MainWidget : public QWidget
{
    Q_OBJECT

public:
    MainWidget(QWidget *parent = nullptr);
    ~MainWidget();
private:
    Ui::MainWidget *ui;

protected:
    virtual void closeEvent(QCloseEvent *e);

private slots:
    void executeGdbCommand();
    void ensureTreeViewVisible(const QString& fullpath);
    void setItemsEnable(bool en);
    void enableGuiItems() { setItemsEnable(true); }
    void disableGuiItems() { setItemsEnable(false); }
    void updateSourceFiles();
    bool openFile(const QString& fullpath);
    void toggleBreakpointAt(const QString& file, int line);

    void buttonAddWatchClicked();
    void buttonDelWatchClicked();
    void buttonClrWatchClicked();

    void editorMarginClicked(int margin, int line, Qt::KeyboardModifiers);
    void fileViewActivate(const QModelIndex& idx);
    void stackTraceClicked(const QModelIndex& idx);

    void startDebuggin();
    void triggerUpdateContext();
    void toggleRunStop();

    void debugUpdateLocalVariables(const QList<gdb::Variable>& locals);
    void debugUpdateCurrentFrame(const gdb::Frame& frame);
    void debugUpdateThreads(int curr, const QList<gdb::Thread>& threads);
    void debugUpdateStackFrame(const QList<gdb::Frame>& stackTrace);
    void debugAsyncStopped(const gdb::AsyncContext &ctx);
    void debugAsyncRunning();

    void debugBreakInserted(const gdb::Breakpoint& bp);
    void debugBreakRemoved(const gdb::Breakpoint& bp);

    void debugVariableCreated(const gdb::Variable& var);
    void debugVariableRemoved(const gdb::Variable& var);
    void debugVariablesUpdate(const QStringList& changes);
};
#endif // WIDGET_H
