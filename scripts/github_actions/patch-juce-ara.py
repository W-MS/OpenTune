#!/usr/bin/env python3
"""Apply the ARA 2.3.0 include rename required by BUILDING.md."""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
JUCE = ROOT / "JUCE-master"
TARGET_RELATIVE = pathlib.Path("modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp")
TARGET = JUCE / TARGET_RELATIVE
OLD = b"#include <ARA_Library/Utilities/ARAChannelArrangement.cpp>"
NEW = b"#include <ARA_Library/Utilities/ARAChannelFormat.cpp>"


def git(*args: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(JUCE), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        sys.exit(result.stderr.decode("utf-8", errors="replace").strip())
    return result.stdout


def normalized(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


def main() -> None:
    if not TARGET.is_file():
        sys.exit(f"JUCE file not found: {TARGET}")

    upstream = git("show", f"HEAD:{TARGET_RELATIVE.as_posix()}")
    upstream_normalized = normalized(upstream)
    if upstream_normalized.count(OLD) != 1 or NEW in upstream_normalized:
        sys.exit("Pinned JUCE source does not contain exactly one expected ARA include")

    data = TARGET.read_bytes()
    data_normalized = normalized(data)
    expected = upstream_normalized.replace(OLD, NEW, 1)
    status = git("status", "--porcelain", "--untracked-files=all", "--ignored=matching").splitlines()
    expected_status = [b" M " + TARGET_RELATIVE.as_posix().encode("ascii")]

    if data_normalized == upstream_normalized:
        if status:
            sys.exit("JUCE checkout contains changes or untracked files before the ARA patch")
        TARGET.write_bytes(data.replace(OLD, NEW, 1))
        if git("status", "--porcelain", "--untracked-files=all", "--ignored=matching").splitlines() != expected_status:
            sys.exit("JUCE checkout contains changes beyond the expected ARA include patch")
        print("Updated JUCE ARA include to ARAChannelFormat.cpp")
    elif data_normalized == expected:
        if status != expected_status:
            sys.exit("JUCE checkout contains changes beyond the expected ARA include patch")
        print("JUCE ARA include is already ARAChannelFormat.cpp")
    else:
        sys.exit("JUCE ARA source differs from HEAD beyond the expected include replacement")


if __name__ == "__main__":
    main()
