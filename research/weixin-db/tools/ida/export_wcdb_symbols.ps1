[CmdletBinding()]
param(
    [string]$Library = (Join-Path $PSScriptRoot '..\..\.deps\wcdb\build-static-mt-x64\Release\WCDB.lib'),
    [string]$Output = (Join-Path $PSScriptRoot '..\..\artifacts\ida\wcdb-2.1.16-symbols.txt')
)

$ErrorActionPreference = 'Stop'
$resolvedLibrary = [IO.Path]::GetFullPath($Library)
$resolvedOutput = [IO.Path]::GetFullPath($Output)

if (-not (Test-Path -LiteralPath $resolvedLibrary -PathType Leaf)) {
    throw "WCDB library not found: $resolvedLibrary"
}
if (-not (Get-Command llvm-nm -ErrorAction SilentlyContinue)) {
    throw 'llvm-nm is not available in PATH'
}

New-Item -ItemType Directory -Path (Split-Path -Parent $resolvedOutput) -Force |
    Out-Null

$lines = & llvm-nm `
    --defined-only `
    --extern-only `
    --demangle `
    --format=posix `
    $resolvedLibrary
if ($LASTEXITCODE -ne 0) {
    throw "llvm-nm failed with exit code $LASTEXITCODE"
}
[IO.File]::WriteAllLines($resolvedOutput, [string[]]$lines)
Get-Item $resolvedOutput | Select-Object FullName, Length
