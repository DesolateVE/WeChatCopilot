[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\clangd-vcpkg'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\.vscode'),
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VcpkgTriplet = 'x64-windows-static-md',
    [int]$Parallel = 12
)

$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedBuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$resolvedOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$resolvedVcpkgRoot = if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $null
} else {
    [IO.Path]::GetFullPath($VcpkgRoot)
}
$vsDevCmd = Join-Path ${env:ProgramFiles} `
    'Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'

if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
    throw "Visual Studio developer environment not found: $vsDevCmd"
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw 'ninja is not available in PATH'
}
if ($null -eq $resolvedVcpkgRoot) {
    throw 'VCPKG_ROOT is not set. Pass -VcpkgRoot or define VCPKG_ROOT.'
}
$vcpkgToolchain = Join-Path $resolvedVcpkgRoot `
    'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) {
    throw "vcpkg CMake toolchain not found: $vcpkgToolchain"
}

# Import the same MSVC and Windows SDK environment used by the real build.
$environment = & cmd.exe /d /s /c `
    "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) {
    throw 'VsDevCmd failed'
}
foreach ($line in $environment) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        [Environment]::SetEnvironmentVariable(
            $line.Substring(0, $separator),
            $line.Substring($separator + 1),
            'Process')
    }
}

cmake `
    -S $sourceRoot `
    -B $resolvedBuildDirectory `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain" `
    "-DVCPKG_TARGET_TRIPLET=$VcpkgTriplet"
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configuration failed'
}

cmake --build $resolvedBuildDirectory --parallel $Parallel
if ($LASTEXITCODE -ne 0) {
    throw 'CMake build failed'
}

$generatedDatabase = Join-Path $resolvedBuildDirectory 'compile_commands.json'
if (-not (Test-Path -LiteralPath $generatedDatabase -PathType Leaf)) {
    throw "CMake did not generate: $generatedDatabase"
}

New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null
$outputDatabase = Join-Path $resolvedOutputDirectory 'compile_commands.json'
cmake -E copy_if_different $generatedDatabase $outputDatabase
if ($LASTEXITCODE -ne 0) {
    throw 'Could not copy compile_commands.json'
}

Get-Item $outputDatabase | Select-Object FullName, Length, LastWriteTime
