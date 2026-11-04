#!/usr/bin/env bash
# Create and verify a drag-and-drop macOS installer for Liberty.
# Usage: bash Scripts/create-macos-dmg.sh path/to/Liberty.app path/to/Liberty.dmg
set -euo pipefail

if [[ "$#" -ne 2 ]]; then
  echo "Usage: $0 <Liberty.app> <output.dmg>" >&2
  exit 64
fi

app="$1"
output="$2"
if [[ ! -d "$app" || ! -f "$app/Contents/MacOS/Liberty" ]]; then
  echo "Liberty.app is missing or its executable was not built: $app" >&2
  exit 1
fi

mkdir -p "$(dirname "$output")"
staging="$(mktemp -d "${TMPDIR:-/tmp}/liberty-dmg-stage.XXXXXX")"
mountpoint="$(mktemp -d "${TMPDIR:-/tmp}/liberty-dmg-mount.XXXXXX")"
mounted=false
cleanup() {
  if [[ "$mounted" == true ]]; then
    hdiutil detach -quiet "$mountpoint" || true
  fi
  rm -rf "$staging" "$mountpoint"
}
trap cleanup EXIT

# Use ditto to preserve the existing signed application bundle intact.
ditto "$app" "$staging/Liberty.app"
ln -s /Applications "$staging/Applications"
test -L "$staging/Applications"

hdiutil create -quiet \
  -volname "Liberty" \
  -srcfolder "$staging" \
  -format UDZO \
  -imagekey zlib-level=9 \
  -ov "$output"

# Check both the compressed image and the bundle after mounting the image.
hdiutil verify -quiet "$output"
hdiutil attach -quiet -readonly -nobrowse -mountpoint "$mountpoint" "$output"
mounted=true

test -d "$mountpoint/Liberty.app"
test -f "$mountpoint/Liberty.app/Contents/MacOS/Liberty"
test -L "$mountpoint/Applications"
test "$(readlink "$mountpoint/Applications")" = "/Applications"
codesign --verify --deep --strict "$mountpoint/Liberty.app"
lipo -verify_arch arm64 "$mountpoint/Liberty.app/Contents/MacOS/Liberty"

hdiutil detach -quiet "$mountpoint"
mounted=false
echo "Verified Liberty DMG with application and Applications shortcut:"
ls -lh "$output"
