#!/bin/bash
# Submit signed pkg/dmg files to Apple notarytool and staple the tickets.
set -euo pipefail

: "${APPLE_API_KEY_ID:?缺少 Secret APPLE_API_KEY_ID}"
: "${APPLE_API_ISSUER_ID:?缺少 Secret APPLE_API_ISSUER_ID}"
: "${APPLE_API_KEY_P8:?缺少 Secret APPLE_API_KEY_P8}"

if [ "$#" -lt 1 ]; then
    echo "用法: $0 <pkg-or-dmg>..."
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
    echo "APPLE_API_KEY_P8 不是有效的 App Store Connect API 私钥"
    exit 1
fi
chmod 600 "${KEY_PATH}"

notarize_one() {
    local file="$1"
    local json="${RUNNER_TEMP}/notarize.json"
    local err="${RUNNER_TEMP}/notarize.err"
    echo "▶ 提交公证: ${file}"
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
            echo "▶ 公证日志 ${id}"
            xcrun notarytool log "${id}" \
                --key "${KEY_PATH}" \
                --key-id "${APPLE_API_KEY_ID}" \
                --issuer "${APPLE_API_ISSUER_ID}" || true
        fi
        exit "${status}"
    fi
    xcrun stapler staple "${file}"
    xcrun stapler validate "${file}"
    echo "✓ 已公证并装订: ${file}"
}

for file in "$@"; do
    notarize_one "${file}"
done
rm -f "${KEY_PATH}"
