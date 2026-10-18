#!/usr/bin/env python3
"""Apply the ARA 2.3.0 include rename required by BUILDING.md."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET = ROOT / "JUCE-master" / "modules" / "juce_audio_plugin_client" / "juce_audio_plugin_client_ARA.cpp"
OLD = b"#include <ARA_Library/Utilities/ARAChannelArrangement.cpp>"
NEW = b"#include <ARA_Library/Utilities/ARAChannelFormat.cpp>"


def main() -> None:
    if not TARGET.is_file():
        sys.exit(f"JUCE file not found: {TARGET}")
    data = TARGET.read_bytes()
    if OLD in data:
        TARGET.write_bytes(data.replace(OLD, NEW, 1))
        print("Updated JUCE ARA include to ARAChannelFormat.cpp")
    elif NEW in data:
        print("JUCE ARA include is already ARAChannelFormat.cpp")
    else:
        sys.exit(
            "juce_audio_plugin_client_ARA.cpp includes neither "
            "ARAChannelArrangement.cpp nor ARAChannelFormat.cpp"
        )


if __name__ == "__main__":
    main()
