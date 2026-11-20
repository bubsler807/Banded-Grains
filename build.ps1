$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'CMake or Visual Studio C++ tools are required.' }
    $vsPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath 'JUCE\CMakeLists.txt')) {
    & git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
    if ($LASTEXITCODE -ne 0) { throw 'JUCE download failed.' }
}
& $cmakePath -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
& $cmakePath --build build --config Release --target Granular_VST3 GranularEngineTests GranularPluginCheck --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& (Join-Path (Split-Path $cmakePath) 'ctest.exe') --test-dir build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
Write-Host 'Plugin: build\Granular_artefacts\Release\VST3\Banded Grains.vst3'
