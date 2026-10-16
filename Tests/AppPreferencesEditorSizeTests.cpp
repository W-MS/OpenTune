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

bool testLegacyShortcutMigration()
{
    const auto temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("OpenTuneShortcutMigration-" + juce::Uuid().toString());
    OpenTune::AppPreferences::StorageOptions storageOptions;
    storageOptions.settingsDirectory = temporaryDirectory;

    auto settingsFile = temporaryDirectory.getChildFile(storageOptions.fileName);
    juce::PropertiesFile::Options propertiesOptions;
    propertiesOptions.applicationName = settingsFile.getFileNameWithoutExtension();
    propertiesOptions.filenameSuffix = settingsFile.getFileExtension();
    propertiesOptions.folderName = temporaryDirectory.getFullPathName();
    propertiesOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    propertiesOptions.millisecondsBeforeSaving = 0;
    bool legacySaved = false;
    {
        juce::PropertiesFile legacySettings(propertiesOptions);
        legacySettings.setValue("shared.shortcuts.zoomHorizontalIn", "Ctrl+K");
        legacySettings.setValue("shared.shortcuts.zoomVerticalOut", "");
        legacySaved = legacySettings.save();
    }
    if (!legacySaved) {
        temporaryDirectory.deleteRecursively();
        return false;
    }

    const auto horizontalZoomId = OpenTune::KeyShortcutConfig::ShortcutId::ZoomHorizontalIn;
    const auto verticalZoomId = OpenTune::KeyShortcutConfig::ShortcutId::ZoomVerticalOut;
    const auto verticalZoomInId = OpenTune::KeyShortcutConfig::ShortcutId::ZoomVerticalIn;
    const OpenTune::KeyShortcutConfig::KeyBinding customKey(
        'K', juce::ModifierKeys::ctrlModifier);
    const OpenTune::KeyShortcutConfig::KeyBinding wheelBinding(
        OpenTune::KeyShortcutConfig::BindingType::WheelUp,
        juce::ModifierKeys::ctrlModifier);

    bool migrated = false;
    {
        OpenTune::AppPreferences preferences(storageOptions);
        const auto shortcuts = preferences.getState().shared.shortcuts;
        migrated = OpenTune::KeyShortcutConfig::getShortcutBinding(shortcuts, horizontalZoomId)
                       .hasBinding(customKey)
            && OpenTune::KeyShortcutConfig::getShortcutBinding(shortcuts, horizontalZoomId)
                   .hasBinding(wheelBinding)
            && OpenTune::KeyShortcutConfig::getShortcutBinding(shortcuts, verticalZoomId)
                   .bindings.empty()
            && OpenTune::KeyShortcutConfig::getShortcutBinding(shortcuts, verticalZoomInId)
                   .hasBinding({ OpenTune::KeyShortcutConfig::BindingType::WheelUp,
                                 juce::ModifierKeys::shiftModifier });
    }

    bool persistedMigration = false;
    {
        juce::PropertiesFile migratedSettings(propertiesOptions);
        OpenTune::KeyShortcutConfig::ShortcutBinding persistedHorizontalZoom;
        OpenTune::KeyShortcutConfig::ShortcutBinding persistedVerticalZoomIn;
        persistedMigration = migratedSettings.getIntValue("shared.shortcuts.schemaVersion", 0) == 1
            && OpenTune::KeyShortcutConfig::parseShortcutBinding(
                migratedSettings.getValue("shared.shortcuts.zoomHorizontalIn", {}),
                persistedHorizontalZoom)
            && persistedHorizontalZoom.hasBinding(customKey)
            && persistedHorizontalZoom.hasBinding(wheelBinding)
            && OpenTune::KeyShortcutConfig::parseShortcutBinding(
                migratedSettings.getValue("shared.shortcuts.zoomVerticalIn", {}),
                persistedVerticalZoomIn)
            && persistedVerticalZoomIn.hasBinding({ OpenTune::KeyShortcutConfig::BindingType::WheelUp,
                                                    juce::ModifierKeys::shiftModifier })
            && migratedSettings.containsKey("shared.shortcuts.zoomVerticalOut")
            && migratedSettings.getValue("shared.shortcuts.zoomVerticalOut", {}).isEmpty();
    }
    temporaryDirectory.deleteRecursively();
    return migrated && persistedMigration;
}

} // namespace

int main()
{
    if (!testSharedPluginEditorSize()) {
        std::fputs("FAIL: shared plugin editor size preferences\n", stderr);
        return 1;
    }

    if (!testLegacyShortcutMigration()) {
        std::fputs("FAIL: legacy shortcut schema migration\n", stderr);
        return 1;
    }

    std::puts("AppPreferences tests passed.");
    return 0;
}
