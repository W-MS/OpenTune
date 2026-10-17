#!/bin/bash
# Submit signed pkg/dmg files to Apple notarytool and staple the tickets.
set -euo pipefail

: "${APPLE_API_KEY_ID:?Missing secret APPLE_API_KEY_ID}"
: "${APPLE_API_ISSUER_ID:?Missing secret APPLE_API_ISSUER_ID}"
: "${APPLE_API_KEY_P8:?Missing secret APPLE_API_KEY_P8}"

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <pkg-or-dmg>..."
    exit 1
fi

KEY_PATH="${RUNNER_TEMP}/AuthKey.p8"
compact="$(printf '%s' "${APPLE_API_KEY_P8}" | tr -d '[:space:]')"
if printf '%s' "${compact}" | grep -q '^-----BEGIN'; then
    printf '%s\n' "${APPLE_API_KEY_P8}" > "${KEY_PATH}"
else
    printf '%s' "${compact}" | base64 -D > "${KEY_PATH}"
fi
if ! grep -q "BEGIN PRIVATE KEY" "${KEY_PATH}"; then
    printf '%b\n' "${APPLE_API_KEY_P8}" > "${KEY_PATH}"
fi
if ! grep -q "BEGIN PRIVATE KEY" "${KEY_PATH}"; then
    echo "APPLE_API_KEY_P8 is not a valid App Store Connect API private key"
    exit 1
fi
chmod 600 "${KEY_PATH}"

notarize_one() {
    local file="$1"
    local json="${RUNNER_TEMP}/notarize.json"
    local err="${RUNNER_TEMP}/notarize.err"
    echo "Submitting for notarization: ${file}"
    set +e
    xcrun notarytool submit "${file}" \
        --key "${KEY_PATH}" \
        --key-id "${APPLE_API_KEY_ID}" \
        --issuer "${APPLE_API_ISSUER_ID}" \
        --wait \
        --timeout 30m \
        --output-format json >"${json}" 2>"${err}"
    local status=$?
    set -e
    cat "${json}" "${err}" 2>/dev/null || true
    if [ "${status}" -ne 0 ]; then
        local id=""
        if [ -s "${json}" ]; then
            id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("id",""))' "${json}" 2>/dev/null || true)"
        fi
        if [ -n "${id}" ]; then
            echo "Notarization log ${id}"
            xcrun notarytool log "${id}" \
                --key "${KEY_PATH}" \
                --key-id "${APPLE_API_KEY_ID}" \
                --issuer "${APPLE_API_ISSUER_ID}" || true
        fi
        exit "${status}"
    fi
    xcrun stapler staple "${file}"
    xcrun stapler validate "${file}"
    echo "Notarized and stapled: ${file}"
}

for file in "$@"; do
    notarize_one "${file}"
done
rm -f "${KEY_PATH}"
