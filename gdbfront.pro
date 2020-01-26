QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
CONFIG += qscintilla2

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    debugmanager.cpp \
    dialogabout.cpp \
    dialogstartdebug.cpp \
    main.cpp \
    widget.cpp

HEADERS += \
    debugmanager.h \
    dialogabout.h \
    dialogstartdebug.h \
    widget.h

FORMS += \
    dialogabout.ui \
    dialogstartdebug.ui \
    widget.ui

RESOURCES += \
    resources/images.qrc \
    resources/licences.qrc

unix {
    QMAKE_LFLAGS_RELEASE += -static-libstdc++ -static-libgcc
    QMAKE_LFLAGS_DEBUG += -static-libstdc++ -static-libgcc
    isEmpty(PREFIX) {
        PREFIX = /usr
    }

    target.path = $$PREFIX/bin

    desktopfile.files = gdbfront.desktop
    desktopfile.path = $$PREFIX/share/applications

    iconfiles.files = resources/images/gdbfront.svg resources/images/gdbfront.png
    iconfiles.path = $$PREFIX/share/icons/default/256x256/apps/

    INSTALLS += desktopfile
    INSTALLS += iconfiles
    INSTALLS += target
}

DISTFILES += \
    gdbfront.desktop
