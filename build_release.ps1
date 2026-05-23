param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$solution = Join-Path $root "VstPluginScanner.sln"

$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if (-not $msbuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) {
                $msbuild = $candidate
            }
        }
    }
} else {
    $msbuild = $msbuild.Source
}

if (-not $msbuild) {
    throw "MSBuild.exe wurde nicht gefunden. Installiere Visual Studio 2022 oder Visual Studio Build Tools mit C++ Desktop Development."
}

& $msbuild $solution /m /p:Configuration=$Configuration /p:Platform=$Platform
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild ist mit Exitcode $LASTEXITCODE fehlgeschlagen."
}

$exe = Join-Path $root "$Platform\$Configuration\VstPluginScanner.exe"
if (-not (Test-Path $exe)) {
    throw "Build abgeschlossen, aber EXE nicht gefunden: $exe"
}

Write-Host "Build erfolgreich: $exe"
