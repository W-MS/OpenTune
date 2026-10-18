#!/bin/bash
# Download the macOS dependencies listed in BUILDING.md into this runner workspace.
# Nothing here is written back to the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "${ROOT}"
mkdir -p ThirdParty models

JUCE_REF="8.0.6"
JUCE_COMMIT="51a8a6d7aeae7326956d747737ccf1575e61e209"
ARA_REF="releases/2.3.0"
ARA_COMMIT="0005c532bb03eb210e7bb490329fc6d7ba1792c7"
SOUNDTOUCH_REF="2.3.2"
SOUNDTOUCH_COMMIT="29fba832a7920a04eab956b3990c50e13d8c93f9"
SOUNDTOUCH_ARCHIVE_SHA256="ed714f84a3e748de87b24f385ec69d3bdc51ca47b7f4710d2048b84b2761e7ff"

juce_ready=0
if [ -e JUCE-master ]; then
    if juce_head="$(git -C JUCE-master rev-parse HEAD 2>/dev/null)" && \
        [ "${juce_head}" = "${JUCE_COMMIT}" ] && \
        python3 scripts/github_actions/patch-juce-ara.py; then
        juce_ready=1
    else
        rm -rf JUCE-master
    fi
fi
if [ "${juce_ready}" -eq 0 ]; then
    if ! git clone --depth 1 --branch "${JUCE_REF}" \
        https://github.com/juce-framework/JUCE.git JUCE-master; then
        rm -rf JUCE-master
        exit 1
    fi
    juce_head="$(git -C JUCE-master rev-parse HEAD)"
    if [ "${juce_head}" != "${JUCE_COMMIT}" ]; then
        rm -rf JUCE-master
        echo "JUCE ${JUCE_REF} tag revision mismatch: expected ${JUCE_COMMIT}, got ${juce_head}" >&2
        exit 1
    fi
    python3 scripts/github_actions/patch-juce-ara.py
fi

ara_checkout_clean() {
    local status
    if ! status="$(git -C ThirdParty/ARA_SDK-releases-2.3.0 status --porcelain --untracked-files=all --ignored=matching 2>/dev/null)" || \
        [ -n "${status}" ]; then
        return 1
    fi
    if ! git -C ThirdParty/ARA_SDK-releases-2.3.0 submodule status --recursive | \
        awk 'substr($0, 1, 1) != " " { bad = 1 } END { exit bad }'; then
        return 1
    fi
    git -C ThirdParty/ARA_SDK-releases-2.3.0 submodule foreach --quiet --recursive \
        'test -z "$(git status --porcelain --untracked-files=all --ignored=matching)"'
}

ara_ready=0
if [ -e ThirdParty/ARA_SDK-releases-2.3.0 ]; then
    if ara_head="$(git -C ThirdParty/ARA_SDK-releases-2.3.0 rev-parse HEAD 2>/dev/null)" && \
        [ "${ara_head}" = "${ARA_COMMIT}" ] && ara_checkout_clean; then
        ara_ready=1
    else
        rm -rf ThirdParty/ARA_SDK-releases-2.3.0
    fi
fi
if [ "${ara_ready}" -eq 0 ]; then
    if ! git clone --depth 1 --branch "${ARA_REF}" \
        https://github.com/Celemony/ARA_SDK.git ThirdParty/ARA_SDK-releases-2.3.0; then
        rm -rf ThirdParty/ARA_SDK-releases-2.3.0
        exit 1
    fi
    ara_head="$(git -C ThirdParty/ARA_SDK-releases-2.3.0 rev-parse HEAD)"
    if [ "${ara_head}" != "${ARA_COMMIT}" ]; then
        rm -rf ThirdParty/ARA_SDK-releases-2.3.0
        echo "ARA ${ARA_REF} tag revision mismatch: expected ${ARA_COMMIT}, got ${ara_head}" >&2
        exit 1
    fi
    git -C ThirdParty/ARA_SDK-releases-2.3.0 submodule update --init --recursive
    if ! ara_checkout_clean; then
        rm -rf ThirdParty/ARA_SDK-releases-2.3.0
        echo "ARA SDK checkout or submodules are not clean at the pinned revisions" >&2
        exit 1
    fi
fi

fetch_soundtouch_archive() {
    local dest="ThirdParty/soundtouch-2.3.2"
    local marker="${dest}/source/SoundTouch/SoundTouch.cpp"
    local archive parent actual
    archive="$(mktemp)"
    parent="$(mktemp -d)"
    curl -fsSL --retry 5 --retry-delay 5 \
        -o "${archive}" \
        "https://codeberg.org/soundtouch/soundtouch/archive/${SOUNDTOUCH_REF}.tar.gz"
    actual="$(shasum -a 256 "${archive}" | awk '{print $1}')"
    if [ "${actual}" != "${SOUNDTOUCH_ARCHIVE_SHA256}" ]; then
        echo "SoundTouch archive SHA-256 mismatch: expected ${SOUNDTOUCH_ARCHIVE_SHA256}, got ${actual}" >&2
        rm -f "${archive}"
        rm -rf "${parent}"
        return 1
    fi
    tar -xzf "${archive}" -C "${parent}"
    rm -rf "${dest}"
    mv "${parent}/soundtouch" "${dest}"
    rm -f "${archive}"
    rm -rf "${parent}"
    if [ ! -f "${marker}" ]; then
        rm -rf "${dest}"
        echo "SoundTouch archive is missing source/SoundTouch/SoundTouch.cpp" >&2
        return 1
    fi
}

soundtouch_git_clean() {
    local status
    if ! status="$(git -C ThirdParty/soundtouch-2.3.2 status --porcelain --untracked-files=all --ignored=matching 2>/dev/null)" || \
        [ -n "${status}" ]; then
        return 1
    fi
    return 0
}

soundtouch_source_matches() {
    local dest="ThirdParty/soundtouch-2.3.2"
    [ -f "${dest}/source/SoundTouch/SoundTouch.cpp" ] && \
        grep -q 'AC_INIT(\[SoundTouch\],\[2\.3\.2\]' "${dest}/configure.ac"
}

fetch_soundtouch() {
    local dest="ThirdParty/soundtouch-2.3.2"
    local marker="${dest}/source/SoundTouch/SoundTouch.cpp"
    local attempt actual
    if [ -e "${dest}" ]; then
        if [ -e "${dest}/.git" ]; then
            if actual="$(git -C "${dest}" rev-parse HEAD 2>/dev/null)" && \
                [ "${actual}" = "${SOUNDTOUCH_COMMIT}" ] && soundtouch_git_clean; then
                if [ -f "${marker}" ]; then
                    return 0
                fi
            fi
            rm -rf "${dest}"
        elif soundtouch_source_matches; then
            return 0
        else
            rm -rf "${dest}"
            fetch_soundtouch_archive
            return
        fi
    fi
    for attempt in 1 2 3 4 5; do
        rm -rf "${dest}"
        if git clone --depth 1 --branch "${SOUNDTOUCH_REF}" \
            https://codeberg.org/soundtouch/soundtouch.git "${dest}"; then
            actual="$(git -C "${dest}" rev-parse HEAD)"
            if [ "${actual}" != "${SOUNDTOUCH_COMMIT}" ]; then
                rm -rf "${dest}"
                echo "SoundTouch ${SOUNDTOUCH_REF} tag revision mismatch: expected ${SOUNDTOUCH_COMMIT}, got ${actual}" >&2
                return 1
            fi
            if ! soundtouch_git_clean || [ ! -f "${marker}" ]; then
                rm -rf "${dest}"
                echo "SoundTouch checkout is not clean at the pinned revision" >&2
                return 1
            fi
            return 0
        fi
        echo "SoundTouch git clone failed (attempt ${attempt}/5)"
        sleep $((attempt * 5))
    done
    echo "Falling back to the SoundTouch ${SOUNDTOUCH_REF} source archive"
    fetch_soundtouch_archive
}

fetch_soundtouch

python3 scripts/github_actions/fetch-models.py

test -f Installer/NOTICE.md
test -f ThirdParty/r8brain-free-src-master/r8bbase.h
test -f ThirdParty/soundtouch-2.3.2/source/SoundTouch/SoundTouch.cpp
test -d ThirdParty/ARA_SDK-releases-2.3.0/ARA_Library
echo "macOS source dependencies are in the runner workspace. ONNX Runtime is built separately by scripts/build-onnxruntime-macos.sh."
