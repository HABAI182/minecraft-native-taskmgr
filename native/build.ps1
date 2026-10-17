param([string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
$mcVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $mcVswhere)) { throw '请安装 Visual Studio 2022 的 C++ 桌面开发组件。' }
$mcVsPath = & $mcVswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $mcVsPath) { throw '缺少 Visual Studio 2022 的 C++ x64 构建工具；本脚本使用 VS 17 2022 生成器。' }
$mcCmake = Join-Path $mcVsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $mcCmake)) {
    $mcCmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $mcCmakeCommand) { throw '缺少 CMake，请安装 VS 的 C++ CMake 工具。' }
    $mcCmake = $mcCmakeCommand.Source
}
& $mcCmake -S $PSScriptRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。' }
& $mcCmake --build $BuildDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
Write-Host "客户端：$(Join-Path $BuildDirectory 'Release\MCNativeTaskmgr.exe')"
