#!/bin/bash
# Decode the Developer ID PKCS#12 secrets into a temporary keychain on this runner.
set -euo pipefail

: "${MACOS_APPLICATION_P12_BASE64:?Missing secret MACOS_APPLICATION_P12_BASE64}"
: "${MACOS_APPLICATION_P12_PASSWORD:?Missing secret MACOS_APPLICATION_P12_PASSWORD}"
: "${MACOS_INSTALLER_P12_BASE64:?Missing secret MACOS_INSTALLER_P12_BASE64}"
: "${MACOS_INSTALLER_P12_PASSWORD:?Missing secret MACOS_INSTALLER_P12_PASSWORD}"
: "${APPLE_TEAM_ID:?Missing secret APPLE_TEAM_ID}"

APPLE_TEAM_ID="$(printf '%s' "${APPLE_TEAM_ID}" | tr -d '[:space:]')"
KEYCHAIN_PATH="${RUNNER_TEMP}/opentune-signing.keychain-db"
KEYCHAIN_PASSWORD="$(openssl rand -base64 32)"
APP_P12="${RUNNER_TEMP}/application.p12"
INST_P12="${RUNNER_TEMP}/installer.p12"

decode_p12() {
    printf '%s' "$1" | tr -d '[:space:]' | base64 -D > "$2"
    local magic
    magic="$(od -An -tx1 -N 1 "$2" | tr -d '[:space:]')"
    if [ "${magic}" != "30" ]; then
        echo "Decoded certificate is not PKCS#12: $2"
        exit 1
    fi
}

decode_p12 "${MACOS_APPLICATION_P12_BASE64}" "${APP_P12}"
decode_p12 "${MACOS_INSTALLER_P12_BASE64}" "${INST_P12}"

security delete-keychain "${KEYCHAIN_PATH}" >/dev/null 2>&1 || true
security create-keychain -p "${KEYCHAIN_PASSWORD}" "${KEYCHAIN_PATH}"
security set-keychain-settings -lut 21600 "${KEYCHAIN_PATH}"
security unlock-keychain -p "${KEYCHAIN_PASSWORD}" "${KEYCHAIN_PATH}"
security list-keychains -d user -s "${KEYCHAIN_PATH}"
security default-keychain -s "${KEYCHAIN_PATH}"

security import "${APP_P12}" -k "${KEYCHAIN_PATH}" -P "${MACOS_APPLICATION_P12_PASSWORD}" -A -t cert -f pkcs12
security import "${INST_P12}" -k "${KEYCHAIN_PATH}" -P "${MACOS_INSTALLER_P12_PASSWORD}" -A -t cert -f pkcs12
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "${KEYCHAIN_PASSWORD}" "${KEYCHAIN_PATH}"

SIGN_IDENTITY="$(security find-identity -v -p codesigning "${KEYCHAIN_PATH}" | sed -n 's/.*"\(Developer ID Application:[^"]*\)"/\1/p' | head -n 1)"
PKG_SIGN_IDENTITY="$(security find-identity -v "${KEYCHAIN_PATH}" | sed -n 's/.*"\(Developer ID Installer:[^"]*\)"/\1/p' | head -n 1)"

if [ -z "${SIGN_IDENTITY}" ] || [ -z "${PKG_SIGN_IDENTITY}" ]; then
    echo "Keychain is missing Developer ID Application or Developer ID Installer"
    security find-identity -v "${KEYCHAIN_PATH}" || true
    exit 1
fi

case "${SIGN_IDENTITY}" in
    *"($APPLE_TEAM_ID)"*) ;;
    *) echo "Application certificate Team ID does not match APPLE_TEAM_ID"; exit 1 ;;
esac
case "${PKG_SIGN_IDENTITY}" in
    *"($APPLE_TEAM_ID)"*) ;;
    *) echo "Installer certificate Team ID does not match APPLE_TEAM_ID"; exit 1 ;;
esac

echo "Application: ${SIGN_IDENTITY}"
echo "Installer: ${PKG_SIGN_IDENTITY}"

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "SIGN_IDENTITY=${SIGN_IDENTITY}"
        echo "PKG_SIGN_IDENTITY=${PKG_SIGN_IDENTITY}"
    } >> "${GITHUB_ENV}"
fi
