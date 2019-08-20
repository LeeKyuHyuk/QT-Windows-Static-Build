. "$PSScriptRoot\..\common\windows\03-conan.ps1"

Run-Conan-Install `
    -ConanfilesDir "$PSScriptRoot\conanfiles" `
    -BuildinfoDir MSVC2015-x86_64 `
    -Arch x86_64 `
    -Compiler "Visual Studio" `
    -CompilerVersion 14 `
    -CompilerRuntime MD

Run-Conan-Install `
    -ConanfilesDir "$PSScriptRoot\conanfiles" `
    -BuildinfoDir MSVC2015-x86 `
    -Arch x86 `
    -Compiler "Visual Studio" `
    -CompilerVersion 14 `
    -CompilerRuntime MD

Run-Conan-Install `
    -ConanfilesDir "$PSScriptRoot\conanfiles" `
    -BuildinfoDir MSVC2017-x86_64 `
    -Arch x86_64 `
    -Compiler "Visual Studio" `
    -CompilerVersion 15 `
    -CompilerRuntime MD

Run-Conan-Install `
    -ConanfilesDir "$PSScriptRoot\conanfiles" `
    -BuildinfoDir MSVC2017-x86 `
    -Arch x86 `
    -Compiler "Visual Studio" `
    -CompilerVersion 15 `
    -CompilerRuntime MD
