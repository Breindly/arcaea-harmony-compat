param(
    [Parameter(Mandatory)]
    [string]$SongsRoot
)

$ErrorActionPreference = 'Stop'

$SongsRoot = (Resolve-Path -LiteralPath $SongsRoot).Path
$repoRoot = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $repoRoot 'entry\src\main\resources\rawfile\library'
$chartRoot = Join-Path $outRoot 'charts'
$jacketRoot = Join-Path $outRoot 'jackets'

foreach ($path in @($chartRoot, $jacketRoot)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

$songListPath = Join-Path $SongsRoot 'songlist'
$packListPath = Join-Path $SongsRoot 'packlist'
if (-not (Test-Path -LiteralPath $songListPath) -or
    -not (Test-Path -LiteralPath $packListPath)) {
    throw 'SongsRoot must contain songlist and packlist files.'
}

$songList = Get-Content -LiteralPath $songListPath -Raw -Encoding UTF8 |
    ConvertFrom-Json
$packList = Get-Content -LiteralPath $packListPath -Raw -Encoding UTF8 |
    ConvertFrom-Json

$packNames = @{}
foreach ($pack in $packList.packs) {
    $name = $pack.name_localized.en
    if ([string]::IsNullOrWhiteSpace($name)) {
        $name = $pack.id
    }
    $packNames[$pack.id] = $name
}

$defaultJacket = Join-Path (Split-Path $SongsRoot -Parent) 'img\default_jacket_256.jpg'
$defaultOut = Join-Path $jacketRoot '_default.jpg'
if (Test-Path -LiteralPath $defaultJacket) {
    Copy-Item -LiteralPath $defaultJacket -Destination $defaultOut -Force
}

$songs = [System.Collections.Generic.List[object]]::new()
$affCount = 0
$jacketCount = 0
$skipped = 0

foreach ($song in $songList.songs) {
    $id = [string]$song.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        continue
    }

    $songDir = Join-Path $SongsRoot $id
    if (-not (Test-Path -LiteralPath $songDir)) {
        $skipped++
        continue
    }

    $difficulties = [System.Collections.Generic.List[object]]::new()
    foreach ($difficulty in $song.difficulties) {
        $ratingClass = [int]$difficulty.ratingClass
        $affPath = Join-Path $songDir "$ratingClass.aff"
        if (-not (Test-Path -LiteralPath $affPath)) {
            continue
        }

        $chartOutDir = Join-Path $chartRoot $id
        New-Item -ItemType Directory -Path $chartOutDir -Force | Out-Null
        Copy-Item -LiteralPath $affPath -Destination (Join-Path $chartOutDir "$ratingClass.aff") -Force
        $affCount++

        $difficulties.Add([PSCustomObject]@{
            rc = $ratingClass
            rating = [string]$difficulty.rating
            designer = [string]$difficulty.chartDesigner
        })
    }

    if ($difficulties.Count -eq 0) {
        $skipped++
        continue
    }

    $jacket = '_default.jpg'
    foreach ($candidate in @('1080_base_256.jpg', 'base_256.jpg', '1080_base.jpg', 'base.jpg')) {
        $candidatePath = Join-Path $songDir $candidate
        if (Test-Path -LiteralPath $candidatePath) {
            $jacket = "$id.jpg"
            Copy-Item -LiteralPath $candidatePath -Destination (Join-Path $jacketRoot $jacket) -Force
            $jacketCount++
            break
        }
    }

    $audio = [System.Collections.Generic.List[string]]::new()
    foreach ($ogg in @('base.ogg', '3.ogg')) {
        if (Test-Path -LiteralPath (Join-Path $songDir $ogg)) {
            $audio.Add($ogg)
        }
    }

    $title = $song.title_localized.en
    if ([string]::IsNullOrWhiteSpace($title)) {
        $title = $song.title_localized.'zh-Hans'
    }
    if ([string]::IsNullOrWhiteSpace($title)) {
        $title = $song.title_localized.ja
    }
    if ([string]::IsNullOrWhiteSpace($title)) {
        $title = $id
    }

    $bpm = 0.0
    try {
        $bpm = [double]$song.bpm_base
    }
    catch {
        $bpm = 0.0
    }

    $songs.Add([PSCustomObject]@{
        id = $id
        title = [string]$title
        titleZh = [string]$song.title_localized.'zh-Hans'
        artist = [string]$song.artist
        bpm = [math]::Round($bpm, 2)
        set = [string]$song.set
        pack = [string]$packNames[$song.set]
        side = [int]$song.side
        jacket = $jacket
        audio = @($audio)
        difficulties = @($difficulties)
    })
}

$library = [PSCustomObject]@{
    version = 1
    generatedAt = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
    songs = @($songs)
}

$json = $library | ConvertTo-Json -Depth 12
$jsonPath = Join-Path $outRoot 'library.json'
[System.IO.File]::WriteAllText($jsonPath, $json, [System.Text.UTF8Encoding]::new($false))

Write-Output (
    'songs={0} charts={1} jackets={2} skipped={3}' -f
    $songs.Count, $affCount, $jacketCount, $skipped
)
Write-Output 'Generated resources are local user data and must not be committed.'
