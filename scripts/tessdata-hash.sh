#!/usr/bin/env bash
#
# tessdata-hash.sh — works out the SHA-256 of the Tesseract model, so it can
# be pinned in fetch-tessdata.ps1.
#
# This exists because the build runs on Windows but the repository is often
# edited from a Mac, where `pwsh` is not installed. All you need from this
# side is the hash: the model itself is fetched by the Windows build.
#
#   ./scripts/tessdata-hash.sh
#
# It downloads to a temporary file, prints the hash, and deletes it again.
# Pass --keep to leave the model in assets/ as well.

set -euo pipefail

URL="https://github.com/tesseract-ocr/tessdata_fast/raw/4.1.0/eng.traineddata"
KEEP=""
[[ "${1:-}" == "--keep" ]] && KEEP=1

cd "$(dirname "$0")/.."

TEMP="$(mktemp)"
# Deleted on every exit path, including Ctrl-C.
trap 'rm -f "$TEMP"' EXIT

echo "Downloading eng.traineddata ..."
curl -fsSL "$URL" -o "$TEMP"

SIZE=$(wc -c < "$TEMP" | tr -d ' ')
if [[ "$SIZE" -lt 1000000 ]]; then
    echo "error: that came back as only $SIZE bytes, which is not a trained model." >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    HASH=$(shasum -a 256 "$TEMP" | awk '{print $1}')
else
    HASH=$(sha256sum "$TEMP" | awk '{print $1}')
fi

if [[ -n "$KEEP" ]]; then
    mkdir -p assets
    cp "$TEMP" assets/eng.traineddata
    echo "Kept a copy at assets/eng.traineddata ($SIZE bytes)."
fi

cat <<EOF

  Size:    $SIZE bytes
  SHA-256: $HASH

  Paste this line into scripts/fetch-tessdata.ps1, replacing the empty one:

      \$ExpectedSha256 = "$HASH"

  Then commit it. CI runs that script with -RequirePinned and will refuse to
  build until the hash is there, so an unverified model cannot end up
  compiled into a release.
EOF
