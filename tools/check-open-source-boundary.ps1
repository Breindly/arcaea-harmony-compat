$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$blockedExtensions = @(
    '.apk', '.hap', '.app', '.hsp',
    '.zip', '.so', '.a', '.dll', '.dylib',
    '.exe', '.obj', '.o', '.dmp',
    '.p12', '.cer', '.p7b', '.pem', '.key'
)

$blockedPaths = @(
    'entry/src/main/resources/rawfile/demo',
    'entry/src/main/resources/rawfile/library/charts',
    'entry/src/main/resources/rawfile/library/jackets'
)

$problems = [System.Collections.Generic.List[string]]::new()

Get-ChildItem -LiteralPath $root -Recurse -File -Force |
    Where-Object {
        $_.FullName -notmatch '\\(\.git|node_modules|oh_modules|build|\.hvigor|\.cache)\\' -and
        $blockedExtensions -contains $_.Extension.ToLowerInvariant()
    } |
    ForEach-Object {
        $problems.Add("Blocked file type: $($_.FullName)")
    }

foreach ($relative in $blockedPaths) {
    $path = Join-Path $root $relative
    if (Test-Path -LiteralPath $path) {
        Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue |
            ForEach-Object {
                $problems.Add("Blocked resource path: $($_.FullName)")
            }
    }
}

if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'Open-source boundary check passed.'
