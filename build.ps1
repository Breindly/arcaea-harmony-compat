param(
    [string]$DevEcoHome = $env:DEVECO_HOME
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($DevEcoHome)) {
    $DevEcoHome = 'D:\DevEco Studio'
}

$sdkHome = Join-Path $DevEcoHome 'sdk'
$javaHome = Join-Path $DevEcoHome 'jbr'
$node = Join-Path $DevEcoHome 'tools\node\node.exe'
$hvigor = Join-Path $DevEcoHome 'tools\hvigor\bin\hvigorw.js'

foreach ($path in @($sdkHome, $javaHome, $node, $hvigor)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "DevEco dependency not found: $path"
    }
}

$env:DEVECO_SDK_HOME = $sdkHome
$env:JAVA_HOME = $javaHome
$env:Path = "$javaHome\bin;$env:Path"

& $node $hvigor assembleHap `
    --mode module `
    -p module=entry@default `
    -p product=default `
    -p buildMode=debug `
    --no-daemon

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Get-ChildItem -LiteralPath 'entry\build\default\outputs\default' -Filter '*.hap' |
    Select-Object FullName, Length, LastWriteTime
