$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vendor = Join-Path $root 'vendor'
$sdk = Join-Path $vendor 'mega-sdk'
$vcpkg = Join-Path $vendor 'vcpkg'
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$sdkRevision = '7e4519cc1d8c42312444482766b761c899894e6f'

$cmakeCommand = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source }
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $cmake = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.CMake.Project `
            -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' |
            Select-Object -First 1
    }
}
if (-not $cmake -or -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found. Install it on PATH or add the CMake component to Visual Studio 2022.'
}

if (-not (Test-Path -LiteralPath $sdk)) {
    New-Item -ItemType Directory -Force -Path $vendor | Out-Null
    git clone --depth 1 https://github.com/meganz/sdk.git $sdk
}

$currentSdkRevision = git -C $sdk rev-parse HEAD
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($currentSdkRevision -ne $sdkRevision) {
    git -C $sdk fetch --depth 1 origin $sdkRevision
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    git -C $sdk checkout --detach $sdkRevision
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path -LiteralPath $vcpkg)) {
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkg
}

$manifest = Get-Content -LiteralPath (Join-Path $sdk 'vcpkg.json') -Raw | ConvertFrom-Json
$baseline = $manifest.'builtin-baseline'
$currentBaseline = git -C $vcpkg rev-parse HEAD
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($currentBaseline -ne $baseline) {
    git -C $vcpkg fetch --depth 1 origin $baseline
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    git -C $vcpkg checkout --detach $baseline
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path -LiteralPath (Join-Path $vcpkg 'vcpkg.exe'))) {
    & (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build --config Release --target SimpleMegaGrabber --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'Release\SimpleMegaGrabber.exe') -Destination $dist -Force
Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE') -Destination (Join-Path $dist 'MEGA_SDK_LICENSE.txt') -Force

$licenseFiles = @(
    @{ Name = 'Crypto++'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\cryptopp\copyright' },
    @{ Name = 'curl'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\curl\copyright' },
    @{ Name = 'ICU'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\icu\copyright' },
    @{ Name = 'libsodium'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\libsodium\copyright' },
    @{ Name = 'SQLite'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\sqlite3\copyright' },
    @{ Name = 'zlib'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\zlib\copyright' },
    @{ Name = 'zstd'; Path = Join-Path $build 'vcpkg_installed\x64-windows-mega\share\zstd\copyright' },
    @{ Name = 'ccronexpr'; Path = Join-Path $sdk 'third_party\ccronexpr\LICENSE' },
    @{ Name = 'csv'; Path = Join-Path $sdk 'third_party\csv\LICENSE' },
    @{ Name = 'http-parser'; Path = Join-Path $sdk 'third_party\http_parser\LICENSE-MIT' },
    @{ Name = 'utf8proc'; Path = Join-Path $sdk 'third_party\utf8proc\LICENSE' },
    @{ Name = 'zxcvbn-c'; Path = Join-Path $sdk 'third_party\zxcvbn-c\LICENSE.txt' }
)

$notices = foreach ($license in $licenseFiles) {
    "===== $($license.Name) ====="
    Get-Content -LiteralPath $license.Path -Raw
}
$notices | Set-Content -LiteralPath (Join-Path $dist 'THIRD_PARTY_NOTICES.txt') -Encoding UTF8

Write-Host "Built: $(Join-Path $dist 'SimpleMegaGrabber.exe')"
