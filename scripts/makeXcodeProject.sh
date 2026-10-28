#!/bin/bash
# Generate the iPadOS Xcode project for running on a device and App Store submission.
#
#   ./scripts/makeXcodeProject.sh [--team TEAM_ID] [--bundle-id ID] [--build-number N] [--no-open]
#
# Downloads the dependencies (JUCE, SoundTouch, ARA headers, AI models, ONNX Runtime
# iOS), then generates Build/ipados-xcode/OpenTune.xcodeproj (Build/ is git-ignored).
# In Xcode: pick the "OpenTuneStandalone_Standalone" scheme and a device, choose your
# team under Signing & Capabilities (or pass --team so it survives regeneration),
# then Run, or Product > Archive > Distribute App > App Store Connect.
#
# Options may also be given as environment variables:
#   OPENTUNE_TEAM_ID, OPENTUNE_IOS_BUNDLE_ID, OPENTUNE_IOS_BUILD_NUMBER
# The build number defaults to a timestamp (YYYYMMDD.HHMM) so every generated
# project can be uploaded; App Store Connect rejects a repeated build number.
#
# The CI workflow (.github/workflows/ipados.yml) does not use this script.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/Build/ipados-xcode"
TEAM_ID="${OPENTUNE_TEAM_ID:-}"
BUNDLE_ID="${OPENTUNE_IOS_BUNDLE_ID:-}"
BUILD_NUMBER="${OPENTUNE_IOS_BUILD_NUMBER:-$(date +%Y%m%d.%H%M)}"
OPEN_PROJECT=1

die() { echo "error: $*" >&2; exit 1; }

usage() {
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --team)         [ $# -ge 2 ] || usage 1; TEAM_ID="$2"; shift 2 ;;
        --bundle-id)    [ $# -ge 2 ] || usage 1; BUNDLE_ID="$2"; shift 2 ;;
        --build-number) [ $# -ge 2 ] || usage 1; BUILD_NUMBER="$2"; shift 2 ;;
        --no-open)      OPEN_PROJECT=0; shift ;;
        -h|--help)      usage 0 ;;
        *)              echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

[ "$(uname -s)" = "Darwin" ] || die "run this script on macOS with Xcode installed"
for tool in xcodebuild xcrun git python3 curl cmake; do
    command -v "${tool}" >/dev/null 2>&1 || {
        [ "${tool}" = "cmake" ] && die "CMake 3.22+ is required (brew install cmake)"
        die "${tool} not found; install Xcode and its command line tools"
    }
done
xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 \
    || die "the iOS SDK is missing; run 'sudo xcode-select -s /Applications/Xcode.app' and install the iOS platform"
[[ "${BUILD_NUMBER}" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]] \
    || die "build number must be up to three dot-separated integers, got '${BUILD_NUMBER}'"

cd "${ROOT}"

# Source files an existing generated project points at under the repo that are
# gone (moved or deleted by a pull). Generated files inside BUILD_DIR are skipped.
missing_project_sources() {
    local pbxproj source
    for pbxproj in "${BUILD_DIR}"/*.xcodeproj/project.pbxproj; do
        [ -f "${pbxproj}" ] || continue
        sed -n 's/.*[[:space:]]path = "\{0,1\}\(\/[^";]*\)"\{0,1\};.*/\1/p' "${pbxproj}" | sort -u |
        while IFS= read -r source; do
            case "${source}" in
                "${BUILD_DIR}"/*) ;;
                "${ROOT}"/*) [ -e "${source}" ] || echo "${source}" ;;
            esac
        done
    done
}

missing="$(missing_project_sources || true)"
if [ -n "${missing}" ]; then
    echo "==> The existing Xcode project references files that no longer exist, e.g."
    echo "    ${missing%%$'\n'*}"
    echo "    Deleting ${BUILD_DIR} (git-ignored) and generating it again."
    rm -rf "${BUILD_DIR}"
fi

echo "==> Downloading dependencies"
bash scripts/github_actions/fetch-macos-deps.sh
bash scripts/github_actions/fetch-ios-onnxruntime.sh

echo "==> Generating Xcode project in ${BUILD_DIR}"
cmake_args=(
    -S "${ROOT}" -B "${BUILD_DIR}" -G Xcode
    -DCMAKE_SYSTEM_NAME=iOS
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
    -DCMAKE_XCODE_GENERATE_SCHEME=ON
    # Archive installs only the app target (it overrides this to NO).
    -DCMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL=YES
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic
    "-DOPENTUNE_IOS_BUILD_NUMBER=${BUILD_NUMBER}"
)
[ -n "${TEAM_ID}" ] && cmake_args+=("-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=${TEAM_ID}")
[ -n "${BUNDLE_ID}" ] && cmake_args+=("-DOPENTUNE_IOS_BUNDLE_ID=${BUNDLE_ID}")
cmake "${cmake_args[@]}"

project="$(find "${BUILD_DIR}" -maxdepth 1 -name '*.xcodeproj' | head -n 1)"
[ -n "${project}" ] || die "CMake did not produce an .xcodeproj in ${BUILD_DIR}"

bundle_id="$(sed -n 's/^OPENTUNE_IOS_BUNDLE_ID:STRING=//p' "${BUILD_DIR}/CMakeCache.txt")"
team="$(sed -n 's/^CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM:[A-Z]*=//p' "${BUILD_DIR}/CMakeCache.txt")"
cat <<EOF

Xcode project: ${project}
  Scheme:       OpenTuneStandalone_Standalone
  Bundle ID:    ${bundle_id}
  Build number: ${BUILD_NUMBER}
  Team:         ${team:-<choose in Xcode: target OpenTuneStandalone_Standalone > Signing & Capabilities>}

Run on an iPad: select the scheme and your device, then Product > Run.
App Store:      Product > Archive, then Distribute App > App Store Connect.
Re-run this script for each upload so the build number increases.
If Xcode already has this project open, close it first: it keeps the old file
list in memory and fails with "Build input file cannot be found".
EOF

if [ "${OPEN_PROJECT}" -eq 1 ]; then
    open "${project}"
fi
