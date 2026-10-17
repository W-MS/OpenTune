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
python3 .github/scripts/patch-juce-ara.py

if [ ! -d ThirdParty/ARA_SDK-releases-2.3.0/.git ]; then
    rm -rf ThirdParty/ARA_SDK-releases-2.3.0
    git clone --depth 1 --branch releases/2.3.0 \
        https://github.com/Celemony/ARA_SDK.git ThirdParty/ARA_SDK-releases-2.3.0
fi
git -C ThirdParty/ARA_SDK-releases-2.3.0 submodule update --init --recursive

if [ ! -f ThirdParty/soundtouch-2.3.3/source/SoundTouch/SoundTouch.cpp ]; then
    rm -rf ThirdParty/soundtouch-2.3.3
    git clone --depth 1 --branch 2.3.3 \
        https://codeberg.org/soundtouch/soundtouch.git ThirdParty/soundtouch-2.3.3
fi

python3 .github/scripts/fetch-models.py

test -f Installer/NOTICE.md
test -f ThirdParty/r8brain-free-src-master/r8bbase.h
test -f ThirdParty/soundtouch-2.3.3/source/SoundTouch/SoundTouch.cpp
test -d ThirdParty/ARA_SDK-releases-2.3.0/ARA_Library
echo "macOS 源码依赖已下载到虚拟机。ONNX Runtime 由 scripts/build-onnxruntime-macos.sh 另行构建。"
