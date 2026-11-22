param(
    [ValidateSet('build', 'flash', 'monitor', 'reconfigure', 'merge-bin')]
    [string]$Action = 'build',
    [string]$Port = 'COM26',
    [ValidateSet('diagnostics', 'session', 'gemini', 'voice', 'microphone', 'question', 'wake', 'followup', 'standalone')]
    [string]$Profile = 'standalone',
    [string]$BuildRoot,
    [switch]$FixtureCheck,
    [switch]$NoCache
)

$ErrorActionPreference = 'Stop'
if ($FixtureCheck -and $Profile -notin @('question', 'followup', 'standalone')) { throw 'Synthetic fixtures are restricted to question, followup and standalone diagnostic profiles.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$externalBuildRoot = if ($BuildRoot) { [System.IO.Path]::GetFullPath($BuildRoot) } else { Join-Path $env:LOCALAPPDATA 'ESP32_Companion' }
$firmwareRoot = Join-Path $projectRoot 'Source\esp32-companion'
$activation = 'C:\Espressif\tools\Microsoft.v5.5.2.PowerShell_profile.ps1'
if (-not (Test-Path -LiteralPath $activation)) {
    throw "ESP-IDF v5.5.2 activation script missing: $activation"
}
. $activation
if ($Action -eq 'flash') {
    if ($Port -ne 'COM26') { throw 'Firmware may be flashed only to the development board on COM26.' }
    $identityOutput = & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" -m esptool --chip esp32s3 --port $Port read_mac 2>&1
    if ($LASTEXITCODE -ne 0 -or ($identityOutput -join "`n") -notmatch '(?i)MAC: 44:bd:8d:ec:38:e0') {
        throw 'COM26 does not match the verified development ESP32; flash cancelled.'
    }
    Write-Output 'Verified development ESP32 identity on COM26.'
}
Push-Location $firmwareRoot
try {
    # Each diagnostic stage builds on the preceding stage, on the same hardware.
    $defaultFiles = @('sdkconfig.defaults', 'sdkconfig.defaults.esp32s3', 'sdkconfig.hardware')
    foreach ($stage in @('diagnostics', 'session', 'gemini', 'voice', 'microphone', 'question', 'wake', 'followup', 'standalone')) {
        $defaultFiles += "config/$stage.defaults"
        if ($stage -eq $Profile) { break }
    }
    # Keep build artifacts outside the synced source workspace by default.
    $profileBuild = Join-Path $externalBuildRoot "build-$Profile"
    $idfArguments = @('-B', $profileBuild, '-DIDF_TARGET=esp32s3',
        "-DSDKCONFIG=$(Join-Path $profileBuild 'sdkconfig')",
        "-DSDKCONFIG_DEFAULTS=$($defaultFiles -join ';')")
    $fixtureOption = if ($FixtureCheck) { 'ON' } else { 'OFF' }
    $idfArguments += "-DCOMPANION_QUESTION_FIXTURE=$fixtureOption"
    if ($NoCache) { $idfArguments += '--no-ccache' }
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
