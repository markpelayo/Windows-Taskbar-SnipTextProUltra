# fetch-tessdata.ps1 — downloads the Tesseract trained model the build embeds.
#
# The model is ~4 MB of binary that is identical for everyone and never
# changes, so it is fetched at build time rather than committed. Keeping it
# out of the history costs one download on a clean checkout and saves every
# clone from carrying it forever.
#
# It is embedded into the executable as a resource, so the *shipped* program
# still has no file beside it and still never touches the network.
#
#   pwsh scripts/fetch-tessdata.ps1 [-Destination assets]

param(
    [string]$Destination = (Join-Path $PSScriptRoot "..\assets"),
    # CI passes this. It turns "no hash pinned" from a warning into a failure,
    # so an unverified blob can never be compiled into a released binary.
    [switch]$RequirePinned
)

$ErrorActionPreference = "Stop"

# tessdata_fast: the integerised smaller network. For screen text — crisp,
# rendered, never scanned — the accuracy gap to tessdata_best is small and the
# 4 MB against 15 MB matters more in a binary people download.
$Url  = "https://github.com/tesseract-ocr/tessdata_fast/raw/4.1.0/eng.traineddata"
$Name = "eng.traineddata"

# ---------------------------------------------------------------------------
# PIN THIS after the first successful fetch.
#
# This file is compiled into the executable, so an unverified download is a
# supply-chain hole, not a convenience. The URL is pinned to a tag rather than
# to main, which stops the content changing under you — but a tag can be
# moved, and only a checksum actually proves what you got.
#
# Leave it empty and the script fetches anyway and prints the hash, so the
# first run tells you exactly what to paste here. Every run after that
# verifies against it and refuses anything else.
#
# On macOS or Linux, where pwsh is usually not installed, use the sibling
# script instead — it prints the same line:  ./scripts/tessdata-hash.sh
# ---------------------------------------------------------------------------
$ExpectedSha256 = "7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2"

$Path = Join-Path $Destination $Name

if (-not (Test-Path $Destination)) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
}

if ((Test-Path $Path) -and $ExpectedSha256) {
    $existing = (Get-FileHash $Path -Algorithm SHA256).Hash.ToLower()
    if ($existing -eq $ExpectedSha256.ToLower()) {
        Write-Host "eng.traineddata is already present and matches its pinned hash."
        exit 0
    }
    Write-Host "eng.traineddata does not match the pinned hash; refetching."
    Remove-Item $Path -Force
} elseif (Test-Path $Path) {
    Write-Host "eng.traineddata is already present (no hash pinned, so not verified)."
    Write-Host "  Its SHA-256 is: $((Get-FileHash $Path -Algorithm SHA256).Hash.ToLower())"
    exit 0
}

Write-Host "Downloading $Name from $Url ..."
Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing

$actual = (Get-FileHash $Path -Algorithm SHA256).Hash.ToLower()

# A sanity floor regardless of pinning: tessdata_fast/eng is about 4 MB, so
# anything tiny is an error page saved with the right filename.
$size = (Get-Item $Path).Length
if ($size -lt 1MB) {
    Remove-Item $Path -Force
    throw "$Name came back as only $size bytes — that is not a trained model. Nothing was kept."
}

if (-not $ExpectedSha256) {
    Write-Host ""
    Write-Host "  Pin it by setting, in scripts/fetch-tessdata.ps1:"
    Write-Host "      `$ExpectedSha256 = `"$actual`""
    Write-Host ""
    if ($RequirePinned) {
        Remove-Item $Path -Force
        throw "No hash is pinned, and -RequirePinned was given. Refusing to compile an unverified model into a build. Paste the hash above into the script and commit it."
    }
    Write-Warning "No hash is pinned, so this download was NOT verified. Fine for a local build; a release will refuse it."
    exit 0
}

if ($actual -ne $ExpectedSha256.ToLower()) {
    Remove-Item $Path -Force
    throw "$Name failed its checksum. Expected $ExpectedSha256, got $actual. Nothing was kept."
}

Write-Host "Fetched $Name and verified it against the pinned hash."
