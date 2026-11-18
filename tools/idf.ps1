param(
    [ValidateSet('build', 'flash', 'monitor', 'reconfigure', 'merge-bin')]
    [string]$Action = 'build',
    [string]$Port = 'COM26',
    [ValidateSet('baseline', 'diagnostics', 'session', 'gemini')]
    [string]$Profile = 'baseline'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwareRoot = Join-Path $projectRoot 'Source\xiaozhi-esp32-2.2.2'
$activation = 'C:\Espressif\tools\Microsoft.v5.5.2.PowerShell_profile.ps1'
if (-not (Test-Path -LiteralPath $activation)) {
    throw "ESP-IDF v5.5.2 activation script missing: $activation"
}
. $activation
if ($Profile -in @('session', 'gemini') -and $Action -eq 'flash') {
    if ($Port -ne 'COM26') { throw 'The session and Gemini profiles may be flashed only to the development board on COM26.' }
    $identityOutput = & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" -m esptool --chip esp32s3 --port $Port read_mac 2>&1
    if ($LASTEXITCODE -ne 0 -or ($identityOutput -join "`n") -notmatch '(?i)MAC: 44:bd:8d:ec:38:e0') {
        throw 'COM26 does not match the verified development ESP32; flash cancelled.'
    }
    Write-Output 'Verified development ESP32 identity on COM26.'
}
Push-Location $firmwareRoot
try {
    $idfArguments = @('-DIDF_TARGET=esp32s3', '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline')
    if ($Profile -eq 'diagnostics') {
        $idfArguments = @('-B', 'build-diagnostics', '-DIDF_TARGET=esp32s3',
            '-DSDKCONFIG=sdkconfig.diagnostics.generated',
            '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline;config/diagnostics.defaults')
    } elseif ($Profile -eq 'session') {
        $idfArguments = @('-B', 'build-session', '-DIDF_TARGET=esp32s3',
            '-DSDKCONFIG=sdkconfig.session.generated',
            '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline;config/diagnostics.defaults;config/session.defaults')
    } elseif ($Profile -eq 'gemini') {
        $idfArguments = @('-B', 'build-gemini', '-DIDF_TARGET=esp32s3',
            '-DSDKCONFIG=sdkconfig.gemini.generated',
            '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline;config/diagnostics.defaults;config/session.defaults;config/gemini.defaults')
    }
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
