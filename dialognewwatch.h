#ifndef DIALOGNEWWATCH_H
#define DIALOGNEWWATCH_H

#include <QDialog>

namespace Ui {
class DialogNewWatch;
}

class DialogNewWatch : public QDialog
{
    Q_OBJECT

public:
    Q_PROPERTY(QString watchName READ watchName WRITE setWatchName)
    Q_PROPERTY(QString watchExpr READ watchExpr WRITE setWatchExpr)

    explicit DialogNewWatch(const QString& expr, QWidget *parent = nullptr);
    ~DialogNewWatch();

    QString watchName() const;

    QString watchExpr() const;

public slots:
    void setWatchName(QString watchName);

    void setWatchExpr(QString watchExpr);

private:
    Ui::DialogNewWatch *ui;
};

#endif // DIALOGNEWWATCH_H
