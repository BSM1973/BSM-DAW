#!/usr/bin/env bash
# Sign the macOS release app with an actual Apple Developer ID Application.
# Required environment: MACOS_CERTIFICATE_P12_BASE64,
# MACOS_CERTIFICATE_PASSWORD, MACOS_SIGNING_IDENTITY.
set -euo pipefail

if [[ "$#" -ne 1 || ! -d "$1" ]]; then
  echo "Usage: $0 <Liberty.app>" >&2
  exit 64
fi
: "${MACOS_CERTIFICATE_P12_BASE64:?Missing Developer ID certificate}"
: "${MACOS_CERTIFICATE_PASSWORD:?Missing certificate password}"
: "${MACOS_SIGNING_IDENTITY:?Missing Developer ID signing identity}"

if [[ "$MACOS_SIGNING_IDENTITY" != "Developer ID Application:"* ]]; then
  echo "Expected a Developer ID Application signing identity" >&2
  exit 1
fi

workdir="$(mktemp -d "${RUNNER_TEMP:-/tmp}/liberty-signing.XXXXXX")"
keychain="$workdir/liberty-signing.keychain-db"
keychain_password="$(openssl rand -hex 24)"
cleanup() {
  security delete-keychain "$keychain" >/dev/null 2>&1 || true
  rm -rf "$workdir"
}
trap cleanup EXIT
printf '%s' "$MACOS_CERTIFICATE_P12_BASE64" | /usr/bin/base64 -D > "$workdir/certificate.p12"
chmod 600 "$workdir/certificate.p12"
security create-keychain -p "$keychain_password" "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 14400 "$keychain"
security import "$workdir/certificate.p12" -k "$keychain" \
  -P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
  -k "$keychain_password" "$keychain"

if ! security find-identity -v -p codesigning "$keychain" \
    | grep -Fq "$MACOS_SIGNING_IDENTITY"; then
  echo "The configured Developer ID identity was not found in the certificate" >&2
  exit 1
fi

# Notarization requires an authentic Developer ID signature, a secure
# timestamp and Hardened Runtime. Never replace these with ad-hoc signing.
codesign --force --deep --options runtime --timestamp \
  --sign "$MACOS_SIGNING_IDENTITY" --keychain "$keychain" "$1"
codesign --verify --deep --strict --verbose=2 "$1"

codesign --display --verbose=4 "$1" 2> "$workdir/signature.txt"
grep -Fq 'Authority=Developer ID Application:' "$workdir/signature.txt"
grep -Eq 'flags=.*runtime' "$workdir/signature.txt"
grep -Fq 'Timestamp=' "$workdir/signature.txt"
echo "Liberty application is signed with Developer ID, Hardened Runtime and timestamp"
