#pragma once

#include <juce_core/juce_core.h>
#include <cctype>
#include <string>

namespace OpenTune {

inline bool sidecarNeedsHighFrequencyOriginal(const juce::StringArray& lines)
{
    bool enabled = false;
    for (const auto& line : lines)
    {
        const auto raw = line.toStdString();
        if (raw.empty() || std::isspace(static_cast<unsigned char>(raw.front())))
            continue;
        const auto colon = raw.find(':');
        if (colon == std::string::npos
            || raw.substr(0, colon) != "needs_high_frequency_original")
            continue;

        auto value = raw.substr(colon + 1);
        const auto comment = value.find('#');
        if (comment != std::string::npos)
            value.erase(comment);
        const auto first = value.find_first_not_of(" \t\r\n");
        const auto last = value.find_last_not_of(" \t\r\n");
        enabled = first != std::string::npos && value.substr(first, last - first + 1) == "true";
    }
    return enabled;
}

} // namespace OpenTune
