[CmdletBinding()]
param([switch]$Staged)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $projectRoot
$descriptor = Get-Content -LiteralPath 'DeftBlade.uproject' -Raw | ConvertFrom-Json
$projectName = $descriptor.Modules[0].Name
$escapedName = [regex]::Escape($projectName)

if ($Staged) {
    $paths = @(& git -c core.quotepath=false diff --cached --name-only --diff-filter=ACMR)
    $revisionPrefix = ':'
} else {
    $paths = @(& git -c core.quotepath=false ls-files)
    $revisionPrefix = 'HEAD:'
}
if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate repository files.' }

$allowedPatterns = @(
    '^\.gitignore$', '^\.gitattributes$', '^\.clang-format$', '^\.editorconfig$', '^README\.md$',
    ('^' + $escapedName + '\.uproject$'),
    '^Source/[A-Za-z0-9_./-]+\.(cpp|h|hpp|inl|cs)$',
    '^Config/Default(Engine|Game|Input)\.ini$',
    ('^Content/' + $escapedName + '/[A-Za-z0-9_./-]+\.(uasset|umap)$'),
    '^Tools/Public/[A-Za-z0-9_./-]+\.(ps1|sh)$',
    '^\.githooks/(pre-commit|pre-push|post-checkout|post-commit|post-merge)$',
    ('^Plugins/' + $escapedName + 'Tools/[A-Za-z0-9_./-]+\.(uplugin|cpp|h|hpp|inl|cs|ini|uasset|umap)$')
)
$blockedPatterns = @(
    '(^|/)(Tests|Binaries|Intermediate|Saved|DerivedDataCache|Developers|ThirdParty|External|PrivateNotes|CourseOriginals)(/|$)',
    '(^|/)(\.codex|\.claude|\.aws|\.ssh|\.vs|\.idea|\.vscode)(/|$)',
    '(^|/)\.env[^/]*$', '(^|/)\.mcp\.json$',
    '\.local\.', 'local\.ini$', '\.(pem|key|pfx|p12|pdb|dll|exe|log|zip|7z|pak|utoc|ucas)$',
    '(^|/)(secrets?|credentials?)([./_-]|$)'
)
$secretPatterns = @(
    '(?i)-----BEGIN (RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----',
    '(?<![A-Z0-9])(AKIA|ASIA)[A-Z0-9]{16}(?![A-Z0-9])',
    'gh[pousr]_[A-Za-z0-9]{36,}',
    'github_pat_[A-Za-z0-9_]{60,}',
    'sk-(proj-)?[A-Za-z0-9_-]{40,}',
    '(?im)(api[_-]?key|client[_-]?secret|access[_-]?token|auth[_-]?token|security[_-]?token|refresh[_-]?token|signing[_-]?key|license[_-]?key|password)\s*[=:]\s*["'']?[A-Za-z0-9_./+=-]{20,}',
    '(?i)[A-Z]:\\Users\\[^\\\s"''<>]+\\',
    '(?i)https?://[^\s/:]+:[^\s/@]+@'
)
$errorsFound = [System.Collections.Generic.List[string]]::new()
foreach ($path in $paths) {
    $allowed = $false
    foreach ($pattern in $allowedPatterns) { if ($path -cmatch $pattern) { $allowed = $true; break } }
    foreach ($pattern in $blockedPatterns) { if ($path -imatch $pattern) { $allowed = $false; break } }
    if (-not $allowed) {
        $errorsFound.Add('Not approved for publication: ' + $path)
        continue
    }
    if ($path -ceq 'README.md') {
        $readmeSize = & git cat-file -s ($revisionPrefix + $path)
        if ($LASTEXITCODE -ne 0) {
            $errorsFound.Add('Could not inspect README.md.')
        } elseif ([long]$readmeSize -ne 0) {
            $errorsFound.Add('README.md must remain empty until documentation is requested.')
        }
        continue
    }
    $blob = @(& git show --no-textconv ($revisionPrefix + $path))
    if ($LASTEXITCODE -ne 0) {
        $errorsFound.Add('Could not inspect staged/committed file: ' + $path)
        continue
    }
    $content = $blob -join "`n"
    if ($path -match '\.(uasset|umap|png)$') {
        if (-not $content.StartsWith('version https://git-lfs.github.com/spec/v1')) {
            $errorsFound.Add('Binary asset must use Git LFS: ' + $path)
        }
        continue
    }
    foreach ($pattern in $secretPatterns) {
        if ($content -match $pattern) {
            $errorsFound.Add('Possible credential or personal path; inspect locally: ' + $path)
            break
        }
    }
}
if ($errorsFound.Count -gt 0) {
    foreach ($message in $errorsFound) { [Console]::Error.WriteLine($message) }
    [Console]::Error.WriteLine('Publication check failed. No matching secret values were printed.')
    exit 1
}
Write-Output ('Publication check passed for ' + $paths.Count + ' file(s).')
exit 0
