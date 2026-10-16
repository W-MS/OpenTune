#pragma once

#include "ContentKey.h"
#include <juce_core/juce_core.h>

namespace OpenTune {

struct OriginalF0FailureEvent
{
    ContentKey contentKey;
    juce::String reason;
};

} // namespace OpenTune
