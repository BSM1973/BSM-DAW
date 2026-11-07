#!/usr/bin/env bash
# Notarize and staple a Developer ID-signed drag-and-drop DMG.
# Required environment: APPLE_NOTARY_KEY_P8_BASE64,
# APPLE_NOTARY_KEY_ID, APPLE_NOTARY_ISSUER_ID.
set -euo pipefail

if [[ "$#" -ne 1 || ! -f "$1" ]]; then
  echo "Usage: $0 <Liberty.dmg>" >&2
  exit 64
fi
: "${APPLE_NOTARY_KEY_P8_BASE64:?Missing App Store Connect API key}"
: "${APPLE_NOTARY_KEY_ID:?Missing App Store Connect key ID}"
: "${APPLE_NOTARY_ISSUER_ID:?Missing App Store Connect issuer ID}"

private_dir="$(mktemp -d "${RUNNER_TEMP:-/tmp}/liberty-notary.XXXXXX")"
trap 'rm -rf "$private_dir"' EXIT
key_file="$private_dir/AuthKey_${APPLE_NOTARY_KEY_ID}.p8"
printf '%s' "$APPLE_NOTARY_KEY_P8_BASE64" | /usr/bin/base64 -D > "$key_file"
chmod 600 "$key_file"

# --wait returns a failure for an invalid submission. Only an accepted
# notarization is stapled and then uploaded to the release artifact.
xcrun notarytool submit "$1" \
  --key "$key_file" \
  --key-id "$APPLE_NOTARY_KEY_ID" \
  --issuer "$APPLE_NOTARY_ISSUER_ID" \
  --wait
xcrun stapler staple "$1"
xcrun stapler validate "$1"
hdiutil verify "$1"
echo "Liberty DMG notarized, stapled and verified by Apple"
