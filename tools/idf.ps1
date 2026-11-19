param(
    [ValidateSet('build', 'flash', 'monitor', 'reconfigure', 'merge-bin')]
    [string]$Action = 'build',
    [string]$Port = 'COM26',
    [ValidateSet('baseline', 'diagnostics', 'session', 'gemini', 'voice', 'microphone', 'question', 'wake', 'followup')]
    [string]$Profile = 'baseline',
    [string]$BuildRoot,
    [switch]$FixtureCheck
)

$ErrorActionPreference = 'Stop'
if ($FixtureCheck -and $Profile -notin @('question', 'followup')) { throw 'Synthetic fixtures are restricted to question and followup diagnostic profiles.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$externalBuildRoot = if ($BuildRoot) { [System.IO.Path]::GetFullPath($BuildRoot) } else { $null }
$firmwareRoot = Join-Path $projectRoot 'Source\xiaozhi-esp32-2.2.2'
$activation = 'C:\Espressif\tools\Microsoft.v5.5.2.PowerShell_profile.ps1'
if (-not (Test-Path -LiteralPath $activation)) {
    throw "ESP-IDF v5.5.2 activation script missing: $activation"
}
. $activation
if ($Profile -in @('session', 'gemini', 'voice', 'microphone', 'question', 'wake', 'followup') -and $Action -eq 'flash') {
    if ($Port -ne 'COM26') { throw 'Cloud diagnostic profiles may be flashed only to the development board on COM26.' }
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
    } elseif ($Profile -eq 'voice') {
        $idfArguments = @('-B', 'build-voice', '-DIDF_TARGET=esp32s3',
            '-DSDKCONFIG=sdkconfig.voice.generated',
            '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline;config/diagnostics.defaults;config/session.defaults;config/gemini.defaults;config/voice.defaults')
    } elseif ($Profile -in @('microphone', 'question', 'wake', 'followup')) {
        $captureDefaults = 'sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline;config/diagnostics.defaults;config/session.defaults;config/gemini.defaults;config/voice.defaults;config/microphone.defaults'
        if ($Profile -in @('question', 'wake', 'followup')) { $captureDefaults += ';config/question.defaults' }
        if ($Profile -in @('wake', 'followup')) { $captureDefaults += ';config/wake.defaults' }
        if ($Profile -eq 'followup') { $captureDefaults += ';config/followup.defaults' }
        $idfArguments = @('-B', "build-$Profile", '-DIDF_TARGET=esp32s3',
            "-DSDKCONFIG=sdkconfig.$Profile.generated", "-DSDKCONFIG_DEFAULTS=$captureDefaults")
    }
    # Keep generated artifacts outside a synced workspace when Windows holds
    # its freshly created dependency files open. Source/config stay here.
    if ($externalBuildRoot) {
        $profileBuild = Join-Path $externalBuildRoot "build-$Profile"
        if ($idfArguments[0] -eq '-B') { $idfArguments[1] = $profileBuild }
        else { $idfArguments = @('-B', $profileBuild) + $idfArguments }
    }
    $fixtureOption = if ($FixtureCheck) { 'ON' } else { 'OFF' }
    $idfArguments += "-DCOMPANION_QUESTION_FIXTURE=$fixtureOption"
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
