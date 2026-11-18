param(
    [ValidateSet('install', 'test', 'check', 'dev', 'login', 'whoami', 'deploy', 'session-check')]
    [string]$Action = 'test'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workerRoot = Join-Path $projectRoot 'cloudflare-worker'
$portableNode = Join-Path $projectRoot '.tools\node-v24.22.3-win-x64\node.exe'
if (Test-Path -LiteralPath $portableNode) {
    $nodeExecutable = $portableNode
} else {
    $nodeCommand = Get-Command node.exe -ErrorAction SilentlyContinue
    if ($null -eq $nodeCommand) {
        throw 'Node.js 22.12+ is required. Install an active LTS release from https://nodejs.org/en/download and reopen PowerShell.'
    }
    $nodeExecutable = $nodeCommand.Source
}
$nodeDirectory = Split-Path -Parent $nodeExecutable
$nodeVersion = & $nodeExecutable --version
if ($LASTEXITCODE -ne 0 -or [version]$nodeVersion.TrimStart('v') -lt [version]'22.12.0') {
    throw 'The Worker tools require Node.js 22.12 or newer.'
}
$npmCli = Join-Path $nodeDirectory 'node_modules\npm\bin\npm-cli.js'
if (-not (Test-Path -LiteralPath $npmCli)) {
    throw 'The Node.js installation must include npm.'
}
if ($Action -ne 'install' -and -not (Test-Path -LiteralPath (Join-Path $workerRoot 'node_modules\wrangler\package.json'))) {
    throw 'Install the pinned Worker dependencies first: .\tools\worker.ps1 -Action install'
}
if ($Action -eq 'deploy') {
    $deployConfig = Get-Content -LiteralPath (Join-Path $workerRoot 'wrangler.jsonc') -Raw | ConvertFrom-Json
    if ($deployConfig.name -ne 'esp32-companion-control') {
        throw 'Deployment is restricted to this project''s esp32-companion-control Worker.'
    }
}

$previousPath = $env:PATH
$previousCfFetch = $env:CLOUDFLARE_CF_FETCH_ENABLED
$previousMetrics = $env:WRANGLER_SEND_METRICS
$previousErrorAction = $ErrorActionPreference
$env:PATH = "$nodeDirectory;$previousPath"
$env:CLOUDFLARE_CF_FETCH_ENABLED = 'false'
$env:WRANGLER_SEND_METRICS = 'false'
Push-Location $workerRoot
try {
    if ($Action -eq 'install') {
        $npmArguments = @('ci', '--no-audit', '--no-fund', '--cache', (Join-Path $projectRoot '.tools\npm-cache'))
    } else {
        $npmArguments = @('run', $Action)
    }
    # Native warnings on stderr are not PowerShell failures; retain the exit code.
    $ErrorActionPreference = 'Continue'
    & $nodeExecutable $npmCli @npmArguments
    $workerExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
    Pop-Location
    $env:PATH = $previousPath
    $env:CLOUDFLARE_CF_FETCH_ENABLED = $previousCfFetch
    $env:WRANGLER_SEND_METRICS = $previousMetrics
}
exit $workerExit
