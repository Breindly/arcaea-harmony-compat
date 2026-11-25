param(
    [Parameter(Mandatory)]
    [string]$SongsRoot,

    [string]$OutFile
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$SongsRoot = (Resolve-Path -LiteralPath $SongsRoot).Path
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\audio.zip'
}

$outDir = Split-Path -Parent $OutFile
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

if (Test-Path -LiteralPath $OutFile) {
    Remove-Item -LiteralPath $OutFile -Force
}

$zip = [System.IO.Compression.ZipFile]::Open(
    $OutFile,
    [System.IO.Compression.ZipArchiveMode]::Create
)

$count = 0
try {
    foreach ($dir in Get-ChildItem -LiteralPath $SongsRoot -Directory) {
        foreach ($name in @('base.ogg', '3.ogg')) {
            $file = Join-Path $dir.FullName $name
            if (-not (Test-Path -LiteralPath $file)) {
                continue
            }

            $entryName = "$($dir.Name)/$name"
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip,
                $file,
                $entryName,
                [System.IO.Compression.CompressionLevel]::NoCompression
            ) | Out-Null

            $count++
        }
    }
}
finally {
    $zip.Dispose()
}

$sizeMB = [math]::Round((Get-Item -LiteralPath $OutFile).Length / 1MB, 1)
Write-Output "zip=$OutFile files=$count sizeMB=$sizeMB"
Write-Output 'The generated ZIP is local user data and must not be committed.'
