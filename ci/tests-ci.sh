#!/bin/bash

set -x

export QTDIR=$(readlink -f /opt/qt*/)
PATH=${QTDIR}/bin:${PATH}

set -e

qmake --version

VERSION=$(git rev-parse --short HEAD)

INSTALL_DIR=/tmp/gdbfront
#DEPLOY_OPT="-no-translations -verbose=2 -executable=$INSTALL_DIR/usr/bin/gdbfront"
DESKTOP_FILE=$INSTALL_DIR/usr/share/applications/gdbfront.desktop

echo ************** LINUX BUILD ***********************

qmake CONFIG+=release CONFIG+=force_debug_info gdbfront.pro
make -j4
make install INSTALL_ROOT=${INSTALL_DIR}

#linuxdeployqt $DESKTOP_FILE $DEPLOY_OPT -appimage
linuxdeployqt $DESKTOP_FILE -appimage
(
APPIMAGE_DIR=${PWD}
APPIMAGE=${PWD}/GdbFront-${VERSION}-x86_64.AppImage
cd /tmp
chmod a+x ${APPIMAGE}
${APPIMAGE} --appimage-extract
mv squashfs-root GdbFront-${VERSION}-x86_64
tar -jcvf ${APPIMAGE_DIR}/GdbFront-${VERSION}-x86_64.tar.bz2 GdbFront-${VERSION}-x86_64
)
echo ************** WINDOWS BUILD ***********************

make distclean
MXE=/usr/lib/mxe/usr
MXEQT=${MXE}/${MXE_TRIPLE}/qt5
PATH=${MXE}/bin:${PATH}
MXE_PKG=gdbfront-${VERSION}-win32
${MXEQT}/bin/qmake CONFIG+=release CONFIG+=force_debug_info gdbfront.pro
make -j4
mkdir ${MXE_PKG}
mv ${PWD}/release/gdbfront.exe ${MXE_PKG}
pydeployqt --objdump ${MXE_TRIPLE}-objdump ${MXE_PKG}/gdbfront.exe \
	--libs ${MXE}/${MXE_TRIPLE}/bin/:${MXEQT}/bin/:${MXEQT}/lib/ \
	--extradll Qt5Svg.dll:Qt5Qml.dll:libjpeg-9.dll \
	--qmake ${MXEQT}/bin/qmake

zip -9 -r ${MXE_PKG}.zip ${MXE_PKG}
