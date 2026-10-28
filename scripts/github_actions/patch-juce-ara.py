#!/usr/bin/env python3
"""Apply the reproducible JUCE source patches required by this repository.

Three patches are applied to the pinned JUCE 8.0.12 checkout in JUCE-master:

1. The ARA 2.3.0 include rename required by BUILDING.md
   (modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp).
2. The AudioDeviceManager temporary-setup API used by
   Source/Standalone/StandaloneAudioDeviceSync.cpp
   (modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.{h,cpp}):
   - new public AudioDeviceManager::setAudioDeviceSetupTemporarily() which
     applies a setup to the running device without touching the per-device-type
     cache (lastDeviceTypeConfigs) or the persisted state (createStateXml()).
   - the existing setAudioDeviceSetup(setup, treatAsChosenDevice) becomes a thin
     wrapper over a new private setAudioDeviceSetupInternal(setup,
     treatAsChosenDevice, updateLastDeviceTypeConfigs).
   - a JUCE_UNIT_TESTS case covering the temporary-setup semantics.
3. The iOS audio device probes sample rates with a playback session. The
   upstream constructor activates AVAudioSessionCategoryPlayAndRecord, which
   makes iOS ask for the microphone as soon as the device object is created,
   even when the app opens zero input channels
   (modules/juce_audio_devices/native/juce_Audio_ios.cpp). open() still
   switches to PlayAndRecord when input channels are requested.

The patches are needle-based: if the pinned upstream content does not contain
the expected context exactly once, the script fails loudly instead of guessing.
After patching, the JUCE checkout must be dirty in exactly the four expected
files and nothing else.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
JUCE = ROOT / "JUCE-master"
JUCE_COMMIT = "29396c22c93392d6738e021b83196283d6e4d850"

ARA_RELATIVE = pathlib.Path(
    "modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp")
ADM_H_RELATIVE = pathlib.Path(
    "modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h")
ADM_CPP_RELATIVE = pathlib.Path(
    "modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
IOS_AUDIO_RELATIVE = pathlib.Path(
    "modules/juce_audio_devices/native/juce_Audio_ios.cpp")

EXPECTED_FILES = [ARA_RELATIVE, ADM_H_RELATIVE, ADM_CPP_RELATIVE, IOS_AUDIO_RELATIVE]

# ---------------------------------------------------------------------------
# Patch 3: iOS device construction must not activate a record session.
# ---------------------------------------------------------------------------
IOS_AUDIO_OLD = b"""        // We need to activate the audio session here to obtain the available sample rates and buffer sizes,
        // but if we don't set a category first then background audio will always be stopped. This category
        // may be changed later.
        setAudioSessionCategory (AVAudioSessionCategoryPlayAndRecord);"""

IOS_AUDIO_NEW = b"""        // Activate a playback session so sample rates and buffer sizes can be
        // queried without prompting for the microphone. open() switches to
        // PlayAndRecord only when input channels are actually requested.
        setAudioSessionCategory (AVAudioSessionCategoryPlayback);"""

# ---------------------------------------------------------------------------
# Patch 1: ARA include rename.
# ---------------------------------------------------------------------------
ARA_OLD = b"#include <ARA_Library/Utilities/ARAChannelArrangement.cpp>"
ARA_NEW = b"#include <ARA_Library/Utilities/ARAChannelFormat.cpp>"

# ---------------------------------------------------------------------------
# Patch 2a: juce_AudioDeviceManager.h.
# ---------------------------------------------------------------------------
ADM_H_TEMPORARY_DECL = b'''    String setAudioDeviceSetup (const AudioDeviceSetup& newSetup, bool treatAsChosenDevice);

    /** Changes the current device or its settings without recording the change.

        This behaves like setAudioDeviceSetup() with treatAsChosenDevice set to false,
        but additionally leaves the per-device-type cached setups untouched, so the
        device type currently in use keeps the settings that were last chosen by the
        user. Use this to apply short-lived settings (for example moving a device to
        the system sample rate just before closing it) that should not become the
        starting point for a later switch back to this device type, and that must not
        be picked up by createStateXml().

        @param newSetup             the settings that you'd like to use, interpreted in
                                    the same way as for setAudioDeviceSetup().
        @returns an error message if anything went wrong, or an empty string if it worked ok.

        @see setAudioDeviceSetup, getAudioDeviceSetup
    */
    String setAudioDeviceSetupTemporarily (const AudioDeviceSetup& newSetup);'''

ADM_H_INTERNAL_DECL = b'''    void stopDevice();

    String setAudioDeviceSetupInternal (const AudioDeviceSetup& newSetup, bool treatAsChosenDevice, bool updateLastDeviceTypeConfigs);

    void updateXml();'''

# ---------------------------------------------------------------------------
# Patch 2b: juce_AudioDeviceManager.cpp.
# ---------------------------------------------------------------------------
# 1) Split the old implementation body out of setAudioDeviceSetup into the new
#    internal helper, and add the temporary wrapper.
ADM_CPP_WRAPPER_OLD = b'''String AudioDeviceManager::setAudioDeviceSetup (const AudioDeviceSetup& newSetup,
                                                bool treatAsChosenDevice)
{
    jassert'''

ADM_CPP_WRAPPER_NEW = b'''String AudioDeviceManager::setAudioDeviceSetup (const AudioDeviceSetup& newSetup,
                                                bool treatAsChosenDevice)
{
    return setAudioDeviceSetupInternal (newSetup, treatAsChosenDevice, true);
}

String AudioDeviceManager::setAudioDeviceSetupTemporarily (const AudioDeviceSetup& newSetup)
{
    // Applies the setup to the running device only: neither the per-device-type
    // cache nor the persisted user settings observe this change.
    return setAudioDeviceSetupInternal (newSetup, false, false);
}

String AudioDeviceManager::setAudioDeviceSetupInternal (const AudioDeviceSetup& newSetup,
                                                        bool treatAsChosenDevice,
                                                        bool updateLastDeviceTypeConfigs)
{
    jassert'''

# 2) Only persist the setup into the per-device-type cache when asked to.
ADM_CPP_CACHE_OLD = b'''        updateCurrentSetup();

        for (int i = 0; i < availableDeviceTypes.size(); ++i)'''

ADM_CPP_CACHE_NEW = b'''        updateCurrentSetup();

        if (updateLastDeviceTypeConfigs)
            for (int i = 0; i < availableDeviceTypes.size(); ++i)'''

# 3) Unit test for the temporary-setup semantics, inserted after the
#    "long sequence of configuration changes" test.
ADM_CPP_TEST_ANCHOR = b'''            closeDeviceByRequestingEmptyNames       (manager);
        }

        beginTest ("AudioDeviceManager updates its current settings before notifying callbacks when device restarts itself");'''

ADM_CPP_TEST_NEW = b'''            closeDeviceByRequestingEmptyNames       (manager);
        }

        beginTest ("A temporary setup does not pollute the per-device-type cache or the saved state");
        {
            AudioDeviceManager manager;
            initialiseManager (manager);

            // The user explicitly chooses a rate; this must become the persisted setting.
            auto userSetup = manager.getAudioDeviceSetup();
            userSetup.sampleRate = 44100.0;
            userSetup.bufferSize = 256;
            userSetup.inputDeviceName = "a";
            userSetup.outputDeviceName = "x";
            userSetup.useDefaultInputChannels = true;
            userSetup.useDefaultOutputChannels = true;

            expect (manager.setAudioDeviceSetup (userSetup, true).isEmpty());

            const auto savedState = manager.createStateXml();
            expect (savedState != nullptr);
            if (savedState != nullptr)
                expectEquals (savedState->getStringAttribute ("audioDeviceRate"),
                              String (44100.0));

            // A temporary setup moves the running device to another supported rate
            // (the mocks expose 44100.0 and 48000.0)...
            auto tempSetup = manager.getAudioDeviceSetup();
            tempSetup.sampleRate = 48000.0;
            expect (manager.setAudioDeviceSetupTemporarily (tempSetup).isEmpty());

            // ...which the running device reports...
            auto current = manager.getAudioDeviceSetup();
            expectEquals (current.sampleRate, 48000.0);

            // ...but the saved user state was not touched.
            const auto savedStateAfterTemp = manager.createStateXml();
            expect (savedStateAfterTemp != nullptr);
            if (savedStateAfterTemp != nullptr)
                expectEquals (savedStateAfterTemp->getStringAttribute ("audioDeviceRate"),
                              String (44100.0));

            // Switching device types and back restores the user's chosen rate.
            manager.setCurrentAudioDeviceType (mockBName, true);
            expectEquals (manager.getCurrentAudioDeviceType(), mockBName);
            expect (manager.getCurrentAudioDevice() != nullptr);

            manager.setCurrentAudioDeviceType (mockAName, true);
            expectEquals (manager.getCurrentAudioDeviceType(), mockAName);

            current = manager.getAudioDeviceSetup();
            expectEquals (current.sampleRate, 44100.0);
            expectEquals (current.bufferSize, 256);
            expectEquals (current.inputDeviceName, String ("a"));
            expectEquals (current.outputDeviceName, String ("x"));

            // The saved state still holds the user's rate.
            const auto savedStateAfterSwitch = manager.createStateXml();
            expect (savedStateAfterSwitch != nullptr);
            if (savedStateAfterSwitch != nullptr)
                expectEquals (savedStateAfterSwitch->getStringAttribute ("audioDeviceRate"),
                              String (44100.0));

            // The legacy API with treatAsChosenDevice=false still updates the
            // per-device-type cache, while leaving createStateXml unchanged.
            auto legacySetup = manager.getAudioDeviceSetup();
            legacySetup.sampleRate = 48000.0;
            expect (manager.setAudioDeviceSetup (legacySetup, false).isEmpty());

            const auto savedStateAfterLegacySetup = manager.createStateXml();
            expect (savedStateAfterLegacySetup != nullptr);
            if (savedStateAfterLegacySetup != nullptr)
                expectEquals (savedStateAfterLegacySetup->getStringAttribute ("audioDeviceRate"),
                              String (44100.0));

            manager.setCurrentAudioDeviceType (mockBName, true);
            manager.setCurrentAudioDeviceType (mockAName, true);
            current = manager.getAudioDeviceSetup();
            expectEquals (current.sampleRate, 48000.0);
        }

        beginTest ("AudioDeviceManager updates its current settings before notifying callbacks when device restarts itself");'''


def git(*args: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(JUCE), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        sys.exit(result.stderr.decode("utf-8", errors="replace").strip())
    return result.stdout


def git_checked_status() -> list:
    return git("status", "--porcelain",
               "--untracked-files=all", "--ignored=matching").splitlines()


def expected_status() -> list:
    return [b" M " + rel.as_posix().encode("ascii") for rel in EXPECTED_FILES]


def normalized(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


def check_newlines(data: bytes, relative: pathlib.Path) -> None:
    lf_only = data.replace(b"\r\n", b"").count(b"\n")
    crlf = data.count(b"\r\n")
    if lf_only and crlf:
        sys.exit(
            f"JUCE {relative.as_posix()} contains mixed line endings "
            "(both LF and CRLF); refusing to rewrite it. Normalize the file "
            "manually before running this script.")


def replace_once(data: bytes, old: bytes, new: bytes, label: str) -> bytes:
    if data.count(old) != 1:
        sys.exit(f"Pinned JUCE source does not contain the expected {label} exactly once")
    return data.replace(old, new, 1)


def main() -> None:
    head = git("rev-parse", "HEAD").decode("ascii").strip()
    if head != JUCE_COMMIT:
        sys.exit(f"Expected JUCE {JUCE_COMMIT}, found {head}")

    transforms = {
        ARA_RELATIVE: [(ARA_OLD, ARA_NEW, "ARA include")],
        ADM_H_RELATIVE: [
            (b"    String setAudioDeviceSetup (const AudioDeviceSetup& newSetup, bool treatAsChosenDevice);",
             ADM_H_TEMPORARY_DECL, "temporary setup declaration"),
            (b"    void stopDevice();\n\n    void updateXml();",
             ADM_H_INTERNAL_DECL, "internal declaration"),
        ],
        ADM_CPP_RELATIVE: [
            (ADM_CPP_WRAPPER_OLD, ADM_CPP_WRAPPER_NEW, "setup wrapper split"),
            (ADM_CPP_CACHE_OLD, ADM_CPP_CACHE_NEW, "per-device-type cache guard"),
            (ADM_CPP_TEST_ANCHOR, ADM_CPP_TEST_NEW, "temporary setup unit test"),
        ],
        IOS_AUDIO_RELATIVE: [
            (IOS_AUDIO_OLD, IOS_AUDIO_NEW, "iOS playback session probe"),
        ],
    }

    outputs = {}
    status_before = []
    for relative, replacements in transforms.items():
        target = JUCE / relative
        if not target.is_file():
            sys.exit(f"JUCE file not found: {target}")

        upstream = normalized(git("show", f"HEAD:{relative.as_posix()}"))
        expected = upstream
        for old, new, label in replacements:
            expected = replace_once(expected, old, new, label)

        original = target.read_bytes()
        check_newlines(original, relative)
        current = normalized(original)
        if current != upstream and current != expected:
            sys.exit(f"JUCE {relative.as_posix()} differs from the pinned source and expected patch")

        outputs[relative] = (target, original, upstream, expected)
        if current != upstream:
            status_before.append(b" M " + relative.as_posix().encode("ascii"))

    status = git_checked_status()
    if sorted(status) != sorted(status_before):
        sys.exit(
            "JUCE checkout contains changes beyond the expected patches; git status is:\n"
            + "\n".join(s.decode("utf-8", errors="replace") for s in status))

    # Validate every input before writing any file, so source drift cannot leave
    # a partially patched dependency checkout.
    for relative, (target, original, upstream, expected) in outputs.items():
        if normalized(original) == expected:
            continue
        newline = b"\r\n" if b"\r\n" in original else b"\n"
        target.write_bytes(expected.replace(b"\n", newline))

    status_after = git_checked_status()
    if sorted(status_after) != sorted(expected_status()):
        sys.exit(
            "JUCE checkout contains changes beyond the expected patches; git status is:\n"
            + "\n".join(s.decode("utf-8", errors="replace") for s in status_after))

    print("JUCE patches applied: ARA include + AudioDeviceManager temporary setup "
          "+ iOS playback session probe (exactly 4 files dirty)")


if __name__ == "__main__":
    main()
