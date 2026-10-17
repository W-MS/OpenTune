#!/bin/bash
# Download the macOS dependencies listed in BUILDING.md into this runner workspace.
# Nothing here is written back to the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "${ROOT}"
mkdir -p ThirdParty models

if [ ! -f JUCE-master/CMakeLists.txt ]; then
    rm -rf JUCE-master
    git clone --depth 1 https://github.com/juce-framework/JUCE.git JUCE-master
fi
python3 scripts/github_actions/patch-juce-ara.py

if [ ! -d ThirdParty/ARA_SDK-releases-2.3.0/.git ]; then
    rm -rf ThirdParty/ARA_SDK-releases-2.3.0
    git clone --depth 1 --branch releases/2.3.0 \
        https://github.com/Celemony/ARA_SDK.git ThirdParty/ARA_SDK-releases-2.3.0
fi
git -C ThirdParty/ARA_SDK-releases-2.3.0 submodule update --init --recursive

fetch_soundtouch() {
    local dest="ThirdParty/soundtouch-2.3.3"
    local marker="${dest}/source/SoundTouch/SoundTouch.cpp"
    local attempt archive parent
    if [ -f "${marker}" ]; then
        return 0
    fi
    for attempt in 1 2 3 4 5; do
        rm -rf "${dest}"
        if git clone --depth 1 --branch 2.3.3 \
            https://codeberg.org/soundtouch/soundtouch.git "${dest}"; then
            return 0
        fi
        echo "SoundTouch git clone failed (attempt ${attempt}/5)"
        sleep $((attempt * 5))
    done
    echo "Falling back to the SoundTouch 2.3.3 source archive"
    archive="$(mktemp)"
    parent="$(mktemp -d)"
    curl -fsSL --retry 5 --retry-delay 5 \
        -o "${archive}" \
        "https://codeberg.org/soundtouch/soundtouch/archive/2.3.3.tar.gz"
    tar -xzf "${archive}" -C "${parent}"
    rm -rf "${dest}"
    mv "${parent}/soundtouch" "${dest}"
    rm -f "${archive}"
    rm -rf "${parent}"
    test -f "${marker}"
}

fetch_soundtouch

python3 scripts/github_actions/fetch-models.py

test -f Installer/NOTICE.md
test -f ThirdParty/r8brain-free-src-master/r8bbase.h
test -f ThirdParty/soundtouch-2.3.3/source/SoundTouch/SoundTouch.cpp
test -d ThirdParty/ARA_SDK-releases-2.3.0/ARA_Library
echo "macOS source dependencies are in the runner workspace. ONNX Runtime is built separately by scripts/build-onnxruntime-macos.sh."
