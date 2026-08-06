[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$FlairDirectory,
    [string]$Library = (Join-Path $PSScriptRoot '..\..\.deps\wcdb\build-static-mt-x64\Release\WCDB.lib'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\artifacts\ida'),
    [string]$IdaDirectory
)

$ErrorActionPreference = 'Stop'
$pcf = Join-Path $FlairDirectory 'pcf.exe'
$sigmake = Join-Path $FlairDirectory 'sigmake.exe'
$resolvedLibrary = [IO.Path]::GetFullPath($Library)
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)

foreach ($required in @($pcf, $sigmake, $resolvedLibrary)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null
$pattern = Join-Path $resolvedOutput 'wcdb-2.1.16-msvc-x64.pat'
$signature = Join-Path $resolvedOutput 'wcdb-2.1.16-msvc-x64.sig'
$exclusions = Join-Path $resolvedOutput 'wcdb-2.1.16-msvc-x64.exc'

& $pcf $resolvedLibrary $pattern
if ($LASTEXITCODE -ne 0) {
    throw "pcf failed with exit code $LASTEXITCODE"
}

& $sigmake '-r' '-nWCDB 2.1.16 MSVC x64' $pattern $signature
if ($LASTEXITCODE -ne 0) {
    if (Test-Path -LiteralPath $exclusions) {
        throw @"
sigmake found name/pattern collisions.
Review and edit:
  $exclusions
Then rerun this script. FLAIR keeps that file as the collision policy.
"@
    }
    throw "sigmake failed with exit code $LASTEXITCODE"
}

Get-Item $pattern, $signature | Select-Object FullName, Length

if ($IdaDirectory) {
    $idaSignatureDirectory = Join-Path $IdaDirectory 'sig\pc'
    if (-not (Test-Path -LiteralPath $idaSignatureDirectory -PathType Container)) {
        throw "IDA PC signature directory not found: $idaSignatureDirectory"
    }

    $installedSignature = Join-Path $idaSignatureDirectory (Split-Path -Leaf $signature)
    if (Test-Path -LiteralPath $installedSignature) {
        $sourceHash = (Get-FileHash -LiteralPath $signature -Algorithm SHA256).Hash
        $installedHash = (
            Get-FileHash -LiteralPath $installedSignature -Algorithm SHA256
        ).Hash
        if ($sourceHash -ne $installedHash) {
            throw "Refusing to overwrite a different signature: $installedSignature"
        }
    } else {
        Copy-Item -LiteralPath $signature -Destination $installedSignature
    }
    Write-Host "Installed IDA signature: $installedSignature"
}

Write-Host ''
Write-Host 'In IDA: File -> Load file -> FLIRT signature file, then select:'
Write-Host "  $signature"
