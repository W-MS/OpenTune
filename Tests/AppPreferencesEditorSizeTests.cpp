#include "Utils/AppPreferences.h"

#include <cstdio>

namespace {

bool hasSize(const OpenTune::PluginEditorSize& actual, int width, int height)
{
    return actual.width == width && actual.height == height;
}

bool testSharedPluginEditorSize()
{
    const auto temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("OpenTuneAppPreferences-" + juce::Uuid().toString());
    OpenTune::AppPreferences::StorageOptions options;
    options.settingsDirectory = temporaryDirectory;

    bool sharedInstancesReadLatestSize = false;
    {
        OpenTune::AppPreferences instanceA(options);
        OpenTune::AppPreferences instanceB(options);
        OpenTune::AppPreferences staleInstance(options);
        const auto initialSize = instanceB.getDefaultPluginEditorSize();

        instanceA.setDefaultPluginEditorSize({ 1280, 760 });
        const auto refreshedSize = instanceB.getDefaultPluginEditorSize();
        staleInstance.setTheme(OpenTune::ThemeId::DarkBlueGrey);
        sharedInstancesReadLatestSize = hasSize(initialSize, 1000, 900)
            && hasSize(refreshedSize, 1280, 760);
    }

    OpenTune::AppPreferences reopenedInstance(options);
    const auto persistedSize = reopenedInstance.getDefaultPluginEditorSize();
    temporaryDirectory.deleteRecursively();
    return sharedInstancesReadLatestSize && hasSize(persistedSize, 1280, 760);
}

} // namespace

int main()
{
    if (!testSharedPluginEditorSize()) {
        std::fputs("FAIL: shared plugin editor size preferences\n", stderr);
        return 1;
    }

    std::puts("AppPreferences editor size tests passed.");
    return 0;
}
