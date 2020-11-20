#############################################################################
##
## Copyright (C) 2019 The Qt Company Ltd.
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

. "$PSScriptRoot\helpers.ps1"

# This script will install squish package for Windows.
# Squish is need by Release Test Automation (RTA)
# NOTE! Make sure 64bit versions are always installed before 32bit,
# because they use same folder name before a rename

$version = "6.5"

# Qt branch without dot (*.*)
$qtBranch = "513x"
# So far Squish built with Qt5.12 works also with 5.13, but we have to be prepared that on some point
# the compatibility breaks, and we may need to have separate Squish packages for different Qt versions.

$targetDir = "C:\Utils\squish"
$squishUrl = "\\ci-files01-hki.intra.qt.io\provisioning\squish\coin"
$squishBranchUrl = "$squishUrl\$qtBranch"
$testSuite = "suite_test_squish"
$testSuiteUrl = "$squishUrl\$testSuite.7z"

# Squish license
$licensePackage = ".squish-3-license"

$OSVersion = (get-itemproperty -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -Name ProductName).ProductName

Function DownloadAndInstallSquish {
    Param (
        [string]$version,
        [string]$bit,
        [string]$squishPackage
    )
    $SquishUrl = $squishBranchUrl + "\squish-nosignalhandler-" + $version + "-qt" + $qtBranch + "-" + $bit + "-" + $squishPackage + ".exe"
    $SquishInstaller = "$targetDir\$squishPackage.exe"
    $SquishParameters = "unattended=1 targetdir=$targetDir\$squishPackage"

    Write-Host "Fetching from URL $squishUrl"
    Copy-Item "$SquishUrl" "$SquishInstaller"
    Write-Host "Installing Squish"
    $stdoutFile = [System.IO.Path]::GetTempFileName()
    $stderrFile = [System.IO.Path]::GetTempFileName()
    Start-Process -FilePath "$SquishInstaller" -Wait -ArgumentList $SquishParameters -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile | Out-Null
    Remove-Item -Path $SquishInstaller
    if ("$bit" -eq "win64") {
        if ($squishPackage.StartsWith("mingw")) {
            $squishPackage64bit = "mingw_64"
        } else {
            $squishPackage64bit = "$squishPackage`_64"
        }
        Rename-Item $targetDir\$squishPackage $targetDir\$squishPackage64bit
        TestSquish $squishPackage64bit
    } else {
        if ($squishPackage.StartsWith("mingw")) {
            Rename-Item $targetDir\$squishPackage $targetDir\mingw
            TestSquish mingw
        } else {
            TestSquish $squishPackage
        }
    }
}

Function DownloadSquishLicence {
    Param (
        [string]$squishUrl
    )

    Write-Host "Installing Squish license to home directory"
    Copy-Item $squishUrl\$licensePackage ~\$licensePackage
}

Function TestSquish {
    Param (
        [string]$squishPackage
    )

    Write-Host "Verifying Squish Installation"
    if (cmd /c "$targetDir\$squishPackage\bin\squishrunner.exe --testsuite $targetDir\$testSuite" |Select-String -Pattern "Squish test run successfully") {
        Write-Host "Squish installation tested successfully!"
    } else {
        Write-Host "Squish test failed! $squishPackage wasn't installed correctly."
        [Environment]::Exit(1)
    }
}

Write-Host "Creating $targetDir"
New-Item -ErrorAction Ignore -ItemType directory -Path "$targetDir"

Write-Host "Download and install Test Suite for squish"
Copy-Item $testSuiteUrl $targetDir/$testSuite.7z
Extract-7Zip $targetDir/$testSuite.7z $targetDir

DownloadSquishLicence $squishUrl

if ($OSVersion -eq "Windows 10 Enterprise") {

    if (Is64BitWinHost) {
        DownloadAndInstallSquish $version win64 "msvc14"
        DownloadAndInstallSquish $version win64 "msvc141"
        DownloadAndInstallSquish $version win64 "mingw_gcc73_posix_seh"
    } else {
        DownloadAndInstallSquish $version win32 "mingw_gcc53_posix_dwarf"
    }
    DownloadAndInstallSquish $version win32 "msvc141"

} elseif ($OSVersion -eq "Windows 8.1 Enterprise") {

    if (Is64BitWinHost) {
        DownloadAndInstallSquish $version win64 "msvc14"
    }
    DownloadAndInstallSquish $version win32 "msvc141"

} elseif ($OSVersion -eq "Windows 7 Enterprise") {

    if (Is64BitWinHost) {
        DownloadAndInstallSquish $version win64 "msvc14"
    } else {
        DownloadAndInstallSquish $version win32 "mingw_gcc53_posix_dwarf"
    }
    DownloadAndInstallSquish $version win32 "msvc141"
}
