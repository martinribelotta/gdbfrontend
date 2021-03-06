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
MXE_PKG=GdbFront-${VERSION}-win32
BASE=/home/travis/build/martinribelotta/gdbfrontend
${MXEQT}/bin/qmake CONFIG+=release CONFIG+=force_debug_info gdbfront.pro
make -j4
pydeployqt --objdump ${MXE_TRIPLE}-objdump ${BASE}/build/gdbfront.exe \
	--libs ${MXE}/${MXE_TRIPLE}/bin/:${MXEQT}/bin/:${MXEQT}/lib/ \
	--extradll Qt5Svg.dll:Qt5Qml.dll:libjpeg-9.dll \
	--qmake ${MXEQT}/bin/qmake
mv ${BASE}/build ${MXE_PKG}
wget https://github.com/alirdn/windows-kill/releases/download/1.1.4/windows-kill_Win32_1.1.4_lib_release.zip
unzip -j windows-kill_Win32_1.1.4_lib_release.zip
mv windows-kill.exe ${MXE_PKG}
rm windows-kill_Win32_1.1.4_lib_release.zip
zip -9 -r ${MXE_PKG}.zip ${MXE_PKG}
