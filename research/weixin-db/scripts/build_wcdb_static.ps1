[CmdletBinding()]
param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..\.deps\wcdb'),
    [string]$Generator = 'Visual Studio 18 2026',
    [ValidateSet('x64')]
    [string]$Architecture = 'x64',
    [int]$Parallel = 12
)

$ErrorActionPreference = 'Stop'
$wcdbTag = 'v2.1.16'
$wcdbCommit = 'df808591b9f9a9ab42156006819c3550d5af13a3'
$resolvedSourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$dependencyRoot = Split-Path -Parent $resolvedSourceRoot
$buildRoot = Join-Path $resolvedSourceRoot 'build-static-mt-x64'

if (-not (Test-Path -LiteralPath $resolvedSourceRoot)) {
    New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
    git clone `
        --branch $wcdbTag `
        --depth 1 `
        --recurse-submodules `
        --shallow-submodules `
        https://github.com/Tencent/wcdb.git `
        $resolvedSourceRoot
    if ($LASTEXITCODE -ne 0) {
        throw 'git clone failed'
    }
}

$actualCommit = git -C $resolvedSourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualCommit.Trim() -ne $wcdbCommit) {
    throw "Expected WCDB $wcdbTag at $wcdbCommit, found $actualCommit"
}

git -C $resolvedSourceRoot submodule update --init --recursive --depth 1
if ($LASTEXITCODE -ne 0) {
    throw 'WCDB submodule initialization failed'
}

cmake `
    -S (Join-Path $resolvedSourceRoot 'src') `
    -B $buildRoot `
    -G $Generator `
    -A $Architecture `
    -DBUILD_SHARED_LIBS=OFF `
    -DWCDB_ZSTD=ON `
    -DWCDB_CPP=ON `
    -DWCDB_BRIDGE=OFF `
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
    -DTARGET_NAME=WCDB
if ($LASTEXITCODE -ne 0) {
    throw 'WCDB CMake configuration failed'
}

cmake --build $buildRoot --config Release --parallel $Parallel
if ($LASTEXITCODE -ne 0) {
    throw 'WCDB Release build failed'
}

$releaseRoot = Join-Path $buildRoot 'Release'
Get-Item `
    (Join-Path $releaseRoot 'WCDB.lib'), `
    (Join-Path $releaseRoot 'WCDB.pdb'), `
    (Join-Path $releaseRoot 'sqlcipher.lib'), `
    (Join-Path $releaseRoot 'zstd.lib') |
    Select-Object FullName, Length
