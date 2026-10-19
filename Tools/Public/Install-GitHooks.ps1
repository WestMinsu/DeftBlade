$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $projectRoot
& git config --local core.hooksPath .githooks
if ($LASTEXITCODE -ne 0) { throw 'Could not configure local Git hooks.' }
& git lfs install --local
if ($LASTEXITCODE -ne 0) { throw 'Could not install local Git LFS hooks.' }
Write-Output 'Local publication and Git LFS hooks are installed.'
