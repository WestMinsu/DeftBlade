<#
.SYNOPSIS
Formats DeftBlade's handwritten C++ with the root .clang-format.
.EXAMPLE
.\Tools\Public\Format-Code.ps1
.EXAMPLE
.\Tools\Public\Format-Code.ps1 -Check
.EXAMPLE
.\Tools\Public\Format-Code.ps1 -Files Source/DeftBlade/Combat/DBHealthComponent.h
#>
[CmdletBinding()]
param(
    [switch]$Check,
    [string[]]$Files,
    [string]$ClangFormatPath
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$stylePath = Join-Path $projectRoot '.clang-format'

if ($ClangFormatPath) {
    $formatter = (Get-Command -Name $ClangFormatPath -CommandType Application -ErrorAction Stop).Source
} else {
    $formatter = (Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $formatter) {
        $llvmPath = Join-Path $env:ProgramFiles 'LLVM/bin/clang-format.exe'
        if (Test-Path -LiteralPath $llvmPath) { $formatter = $llvmPath }
    }
    if (-not $formatter) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $formatter = @(& $vswhere -all -products '*' -find 'VC\Tools\Llvm\bin\clang-format.exe' |
                Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)[0]
        }
    }
}
if (-not $formatter) { throw 'clang-format 19+ was not found. Install the Visual Studio C++ Clang tools, or pass -ClangFormatPath.' }
$version = (& $formatter --version) -join ' '
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'version (\d+)' -or [int]$Matches[1] -lt 19) {
    throw ('clang-format 19+ is required. Found: ' + $version)
}
& $formatter "--style=file:$stylePath" --dump-config > $null
if ($LASTEXITCODE -ne 0) { throw 'The .clang-format configuration could not be loaded.' }

$excluded = '(^|/)(Tests|ThirdParty|External|Intermediate|Binaries|Saved|Generated)(/|$)'
$extensions = @('.h', '.hpp', '.cpp', '.inl')
if ($Files) {
    $candidates = @($Files | ForEach-Object {
        $filePath = if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $projectRoot $_ }
        Get-Item -LiteralPath $filePath -ErrorAction Stop
    })
} else {
    $candidates = @(foreach ($folder in @('Source', 'Plugins/DeftBladeTools/Source')) {
        $sourceRoot = Join-Path $projectRoot $folder
        if (Test-Path -LiteralPath $sourceRoot) {
            Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object {
                $relative = $_.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
                $extensions -contains $_.Extension -and $relative -notmatch $excluded -and
                    $_.Name -notmatch '\.(generated\.h|gen\.cpp)$'
            }
        }
    })
}
$targets = @($candidates | ForEach-Object {
    $fullPath = [IO.Path]::GetFullPath($_.FullName)
    if (-not $fullPath.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('File is outside this project: ' + $fullPath)
    }
    $relative = $fullPath.Substring($projectRoot.Length + 1).Replace('\', '/')
    if ($_.PSIsContainer -or $extensions -notcontains $_.Extension -or $relative -notmatch '^(Source/|Plugins/DeftBladeTools/Source/)' -or
        $relative -match $excluded -or $_.Name -match '\.(generated\.h|gen\.cpp)$') {
        throw ('Not a handwritten first-party C++ file: ' + $relative)
    }
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Formatting through a link is not supported: ' + $fullPath) }
    $entry = $_.Directory
    while ($entry -and $entry.FullName -ne $projectRoot) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Formatting through a link is not supported: ' + $fullPath) }
        $entry = $entry.Parent
    }
    $fullPath
} | Sort-Object -Unique)
if ($targets.Count -eq 0) { throw 'No C++ files were selected.' }

$arguments = @("--style=file:$stylePath", '--fallback-style=none')
if ($Check) { $arguments += @('--dry-run', '--Werror') } else { $arguments += '-i' }
$failed = 0
foreach ($target in $targets) {
    & $formatter @arguments $target
    if ($LASTEXITCODE -ne 0) { ++$failed }
}
if ($failed -gt 0) { throw ('Formatting failed for ' + $failed + ' file(s).') }
$verb = if ($Check) { 'Checked' } else { 'Formatted' }
Write-Output ($verb + ' ' + $targets.Count + ' C++ file(s). ' + $version)
