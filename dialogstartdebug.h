#ifndef DIALOGSTARTDEBUG_H
#define DIALOGSTARTDEBUG_H

#include <QDialog>

namespace Ui {
class DialogStartDebug;
}

class DialogStartDebug : public QDialog
{
    Q_OBJECT

public:
    Q_PROPERTY(QString executableFile READ executableFile)
    Q_PROPERTY(QString initScriptName READ initScript)
    Q_PROPERTY(QString initScript READ initScript)
    Q_PROPERTY(bool needWriteInitScript READ needWriteInitScript)
    Q_PROPERTY(QString gdbExecutable READ gdbExecutable)

    explicit DialogStartDebug(QWidget *parent = nullptr);
    ~DialogStartDebug();

    QString executableFile() const;

    QString initScriptName() const;
    QString initScript() const;

    bool needWriteInitScript() const
    {
        return m_needWriteInitScript;
    }

    QString gdbExecutable() const;

public slots:
    void loadInitScript(const QString& path = {});

private:
    Ui::DialogStartDebug *ui;
    bool m_needWriteInitScript;
    QString m_gdbExecutable;
    QString m_initScriptName;
};

#endif // DIALOGSTARTDEBUG_H
