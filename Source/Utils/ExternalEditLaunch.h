#pragma once

#include <string>

namespace OpenTune {

inline constexpr const char* kExternalEditArgument = "--external-edit";

struct ExternalEditLaunch
{
    bool active = false;
    std::string sessionDirectory;
};

// argv[0] is the program name. A following path, or --external-edit=<path>,
// selects the single-track session the standalone should open.
inline ExternalEditLaunch parseExternalEditLaunch(int argc, const char* const* argv) noexcept
{
    ExternalEditLaunch launch;
    if (argv == nullptr)
        return launch;

    const std::string flag(kExternalEditArgument);
    const std::string prefixed = flag + "=";
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr)
            continue;
        const std::string arg(argv[i]);
        if (arg == flag)
        {
            if (i + 1 < argc && argv[i + 1] != nullptr && argv[i + 1][0] != '\0')
            {
                launch.active = true;
                launch.sessionDirectory = argv[i + 1];
            }
            return launch;
        }
        if (arg.compare(0, prefixed.size(), prefixed) == 0 && arg.size() > prefixed.size())
        {
            launch.active = true;
            launch.sessionDirectory = arg.substr(prefixed.size());
            return launch;
        }
    }
    return launch;
}

} // namespace OpenTune
