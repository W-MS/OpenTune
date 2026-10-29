#!/bin/bash
# Download the official ONNX Runtime 1.20.0 iOS package (the onnxruntime-c pod
# archive: static xcframework with the CoreML execution provider) and unpack it
# as ThirdParty/onnxruntime-ios-1.20.0/{include,onnxruntime.xcframework}.
# Nothing here is written back to the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="1.20.0"
URL="https://download.onnxruntime.ai/pod-archive-onnxruntime-c-${VERSION}.zip"
SHA256="50891a8aadd17d4811acb05ed151ba6c394129bb3ab14e843b0fc83a48d450ff"
DEST="${ROOT}/ThirdParty/onnxruntime-ios-${VERSION}"
MARKER="${DEST}/onnxruntime.xcframework/ios-arm64/onnxruntime.framework/onnxruntime"

if [ -f "${MARKER}" ] && [ -f "${DEST}/include/onnxruntime_cxx_api.h" ] \
        && [ -f "${DEST}/include/coreml_provider_factory.h" ]; then
    echo "ONNX Runtime iOS ${VERSION} already present: ${DEST}"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

archive="${work}/onnxruntime-ios.zip"
for attempt in 1 2 3 4 5; do
    if curl -fsSL --retry 3 --retry-delay 2 -o "${archive}" "${URL}"; then
        break
    fi
    if [ "${attempt}" -eq 5 ]; then
        echo "Download failed: ${URL}" >&2
        exit 1
    fi
    sleep $((attempt * 4))
done

actual="$(shasum -a 256 "${archive}" | awk '{print $1}')"
if [ "${actual}" != "${SHA256}" ]; then
    echo "ONNX Runtime iOS archive SHA-256 mismatch: expected ${SHA256}, got ${actual}" >&2
    exit 1
fi

unzip -q "${archive}" -d "${work}/unpacked"

rm -rf "${DEST}"
mkdir -p "${DEST}/onnxruntime.xcframework"
mv "${work}/unpacked/Headers" "${DEST}/include"
mv "${work}/unpacked/LICENSE" "${DEST}/LICENSE"
# Only the iOS slices are linked; the macOS slice in the pod archive is dropped.
mv "${work}/unpacked/onnxruntime.xcframework/Info.plist" "${DEST}/onnxruntime.xcframework/"
mv "${work}/unpacked/onnxruntime.xcframework/ios-arm64" "${DEST}/onnxruntime.xcframework/"
mv "${work}/unpacked/onnxruntime.xcframework/ios-arm64_x86_64-simulator" "${DEST}/onnxruntime.xcframework/"

test -f "${MARKER}"
test -f "${DEST}/include/onnxruntime_cxx_api.h"
test -f "${DEST}/include/coreml_provider_factory.h"
echo "ONNX Runtime iOS ${VERSION} unpacked to ${DEST}"
