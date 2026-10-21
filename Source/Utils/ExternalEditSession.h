#pragma once

#include "ExternalEditLaunch.h"
#include "AutomationLane.h"
#include "Note.h"
#include "PitchCurve.h"
#include "PitchShiftSettings.h"
#include "../Content/ContentKey.h"
#include "../Content/EditableContentSnapshot.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <optional>

namespace OpenTune {

// One plugin clip handed to the standalone editor. The audio file is only
// the audition copy. A returned session carries notes and the pitch curve
// those notes produced; the plugin renders them on its own audio.
struct ExternalEditDocument
{
    ContentKey sourceKey;
    std::vector<Note> notes;
    std::shared_ptr<PitchCurve> pitchCurve;
    AutomationLane envelope;
    PitchShiftSettings pitchShift;
    bool returned = false;
};

class ExternalEditSession
{
public:
    static juce::File createSessionDirectory();
    static bool writeSession(const juce::File& directory,
                             const ContentKey& sourceKey,
                             const EditableContentSnapshot& snapshot);
    static bool writeReturn(const juce::File& directory,
                            const EditableContentSnapshot& snapshot);
    static std::optional<ExternalEditDocument> readIfReturned(const juce::File& directory);
    static std::optional<ExternalEditDocument> readDocument(const juce::File& directory);

    static juce::File audioFile(const juce::File& directory);
    static juce::File findStandaloneExecutable();
    static bool launchStandalone(const juce::File& directory);
    static juce::File sessionDirectoryFromCommandLine();
};

} // namespace OpenTune
