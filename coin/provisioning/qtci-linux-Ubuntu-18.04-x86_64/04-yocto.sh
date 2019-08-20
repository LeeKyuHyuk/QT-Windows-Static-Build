#!/usr/bin/env bash

#############################################################################
##
## Copyright (C) 2017 The Qt Company Ltd.
## Contact: http://www.qt.io/licensing/
##
## This file is part of the provisioning scripts of the Qt Toolkit.
##
## $QT_BEGIN_LICENSE:LGPL21$
## Commercial License Usage
## Licensees holding valid commercial Qt licenses may use this file in
## accordance with the commercial license agreement provided with the
## Software or, alternatively, in accordance with the terms contained in
## a written agreement between you and The Qt Company. For licensing terms
## and conditions see http://www.qt.io/terms-conditions. For further
## information use the contact form at http://www.qt.io/contact-us.
##
## GNU Lesser General Public License Usage
## Alternatively, this file may be used under the terms of the GNU Lesser
## General Public License version 2.1 or version 3 as published by the Free
## Software Foundation and appearing in the file LICENSE.LGPLv21 and
## LICENSE.LGPLv3 included in the packaging of this file. Please review the
## following information to ensure the GNU Lesser General Public License
## requirements will be met: https://www.gnu.org/licenses/lgpl.html and
## http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
##
## As a special exception, The Qt Company gives you certain additional
## rights. These rights are described in The Qt Company LGPL Exception
## version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
##
## $QT_END_LICENSE$
##
#############################################################################

# This script installs the Yocto toolchain

set -ex

# shellcheck source=../common/unix/DownloadURL.sh
source "${BASH_SOURCE%/*}/../common/unix/DownloadURL.sh"
# shellcheck source=../common/unix/SetEnvVar.sh
source "${BASH_SOURCE%/*}/../common/unix/SetEnvVar.sh"

echo "Installing Yocto toolchain for 32-bit b2qt ARMV7..."

versionARM="2.5.2"
package="b2qt-x86_64-meta-toolchain-b2qt-embedded-sdk-qemuarmv7-409ebc3.sh"
PrimaryUrl="http://ci-files01-hki.intra.qt.io/input/boot2qt/sumo/$package"
AltUrl="http://download.qt.io/development_releases/prebuilt/boot2qt/sumo/$package"
SHA1="a03b4eb492b16c6e60cf83c0718c27e9cfa6d522"
yoctoInstaller="/tmp/yocto-toolchain-ARMv7.sh"
yoctoLocationARMv7="/opt/yocto-armv7"
sysrootARMv7="sysroots/armv7ahf-neon-poky-linux-gnueabi"
crosscompileARMv7="sysroots/x86_64-pokysdk-linux/usr/bin/arm-poky-linux-gnueabi/arm-poky-linux-gnueabi-"

DownloadURL "$PrimaryUrl" "$AltUrl" "$SHA1" "$yoctoInstaller"
chmod +x "$yoctoInstaller"

/bin/bash "$yoctoInstaller" -y -d "$yoctoLocationARMv7"
rm -rf "$yoctoInstaller"

echo "Installing Yocto toolchain for 64-bit b2qt ARM64..."

versionARM64="2.5.2"
package="b2qt-x86_64-meta-toolchain-b2qt-embedded-sdk-qemuarm64-409ebc3.sh"
PrimaryUrl="http://ci-files01-hki.intra.qt.io/input/boot2qt/sumo/$package"
AltUrl="http://download.qt.io/development_releases/prebuilt/boot2qt/sumo/$package"
SHA1="3dadf1135d00f7cb5586b605b1f7344ae828c9cd"
yoctoInstaller="/tmp/yocto-toolchain-ARM64.sh"
yoctoLocationARM64="/opt/yocto-arm64"
sysrootARM64="sysroots/aarch64-poky-linux"
crosscompileARM64="sysroots/x86_64-pokysdk-linux/usr/bin/aarch64-poky-linux/aarch64-poky-linux-"

DownloadURL "$PrimaryUrl" "$AltUrl" "$SHA1" "$yoctoInstaller"
chmod +x "$yoctoInstaller"

/bin/bash "$yoctoInstaller" -y -d "$yoctoLocationARM64"
rm -rf "$yoctoInstaller"

echo "Installing Yocto toolchain for 64-bit b2qt MIPS64..."

versionMIPS64="2.5.2"
package="b2qt-x86_64-meta-toolchain-b2qt-embedded-sdk-qemumips64-409ebc3.sh"
PrimaryUrl="http://ci-files01-hki.intra.qt.io/input/boot2qt/sumo/$package"
AltUrl="http://download.qt.io/development_releases/prebuilt/boot2qt/sumo/$package"
SHA1="dd03b496707ca8888b339a667e021722a8c2fdb4"
yoctoInstaller="/tmp/yocto-toolchain-mips64.sh"
yoctoLocationMIPS64="/opt/yocto-mips64"
sysrootMIPS64="sysroots/mips64-poky-linux"
crosscompileMIPS64="sysroots/x86_64-pokysdk-linux/usr/bin/mips64-poky-linux/mips64-poky-linux-"

DownloadURL "$PrimaryUrl" "$AltUrl" "$SHA1" "$yoctoInstaller"
chmod +x "$yoctoInstaller"

/bin/bash "$yoctoInstaller" -y -d "$yoctoLocationMIPS64"
rm -rf "$yoctoInstaller"



if [ -e "$yoctoLocationARMv7/$sysrootARMv7" ] && [ -e "$yoctoLocationARMv7/${crosscompileARMv7}g++" ] && [ -e "$yoctoLocationARM64/$sysrootARM64" ] && [ -e "$yoctoLocationARM64/${crosscompileARM64}g++" ] && [ -e "$yoctoLocationMIPS64/$sysrootMIPS64" ] && [ -e "$yoctoLocationMIPS64/${crosscompileMIPS64}g++" ]; then
    SetEnvVar "QEMUARMV7_TOOLCHAIN_SYSROOT" "$yoctoLocationARMv7/$sysrootARMv7"
    SetEnvVar "QEMUARMV7_TOOLCHAIN_CROSS_COMPILE" "$yoctoLocationARMv7/$crosscompileARMv7"
    SetEnvVar "QEMUARM64_TOOLCHAIN_SYSROOT" "$yoctoLocationARM64/$sysrootARM64"
    SetEnvVar "QEMUARM64_TOOLCHAIN_CROSS_COMPILE" "$yoctoLocationARM64/$crosscompileARM64"
    SetEnvVar "QEMUMIPS64_TOOLCHAIN_SYSROOT" "$yoctoLocationMIPS64/$sysrootMIPS64"
    SetEnvVar "QEMUMIPS64_TOOLCHAIN_CROSS_COMPILE" "$yoctoLocationMIPS64/$crosscompileMIPS64"
else
    echo "Error! Couldn't find installation paths for Yocto toolchain. Aborting provisioning." 1>&2
    exit 1
fi

echo "Yocto ARMv7 toolchain = $versionARM" >> ~/versions.txt
echo "Yocto ARM64 toolchain = $versionARM64" >> ~/versions.txt
echo "Yocto MIPS64 toolchain = $versionMIPS64" >> ~/versions.txt
