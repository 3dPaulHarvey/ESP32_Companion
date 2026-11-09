param(
    [ValidateSet('build', 'flash', 'monitor', 'reconfigure', 'merge-bin')]
    [string]$Action = 'build',
    [string]$Port = 'COM26'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwareRoot = Join-Path $projectRoot 'Source\xiaozhi-esp32-2.2.2'
$activation = 'C:\Espressif\tools\Microsoft.v5.5.2.PowerShell_profile.ps1'
if (-not (Test-Path -LiteralPath $activation)) {
    throw "ESP-IDF v5.5.2 activation script missing: $activation"
}
. $activation
Push-Location $firmwareRoot
try {
    $idfArguments = @('-DIDF_TARGET=esp32s3', '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline')
    if ($Action -in @('flash', 'monitor')) {
        $idfArguments += @('-p', $Port)
    }
    $idfArguments += $Action
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" @idfArguments
    $idfExit = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $idfExit
