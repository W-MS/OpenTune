#include "PlatformFileDialogs.h"

#include "Editor/ConfirmDialogContent.h"
#include "Utils/AppLogger.h"
#include "Utils/LocalizationManager.h"

#include <memory>

namespace OpenTune::PlatformFileDialogs {

namespace {

#if ! JUCE_IOS
juce::File withExtension(const juce::File& file, const juce::String& extension)
{
    return file.hasFileExtension(extension) ? file : file.withFileExtension(extension);
}
#else
juce::String sanitiseBaseName(const juce::String& rawName, const juce::String& extension)
{
    auto name = rawName.trim();
    if (name.endsWithIgnoreCase(extension))
        name = name.dropLastCharacters(extension.length());
    return juce::File::createLegalFileName(name.trim());
}

void promptForFileName(const juce::String& title,
                       const juce::String& defaultBaseName,
                       std::function<void(const juce::String&)> onAccepted)
{
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", defaultBaseName, LOC_RAW("File name"));
    window->addButton(LOC(kOK), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(LOC(kCancel), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // ModalComponentManager 先执行回调再删除 deleteWhenDismissed 的组件，回调里读取输入框是安全的。
    window->enterModalState(true,
        juce::ModalCallbackFunction::create([window, onAccepted = std::move(onAccepted)](int result) {
            if (result == 1)
                onAccepted(window->getTextEditorContents("name"));
        }),
        true);
}

bool isInside(const juce::File& file, const juce::File& directory)
{
    return file == directory || file.isAChildOf(directory);
}

bool copyUrlToFile(const juce::URL& url, const juce::File& destination)
{
    auto input = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
    if (input == nullptr)
        return false;

    juce::TemporaryFile temporary(destination);
    {
        juce::FileOutputStream output(temporary.getFile());
        if (!output.openedOk())
            return false;
        if (output.writeFromInputStream(*input, -1) < 0)
            return false;
        output.flush();
        if (output.getStatus().failed())
            return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}
#endif

} // namespace

juce::File getAppDocumentsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
}

void chooseSaveTarget(const juce::String& title,
                      const juce::File& desktopInitialFile,
                      const juce::String& extension,
                      const juce::String& mobileSubdirectory,
                      SaveTargetCallback onChosen)
{
#if JUCE_IOS
    const auto defaultBaseName = desktopInitialFile.getFileNameWithoutExtension().isNotEmpty()
        ? desktopInitialFile.getFileNameWithoutExtension()
        : juce::String("Untitled");

    promptForFileName(title, defaultBaseName,
        [extension, mobileSubdirectory, onChosen = std::move(onChosen)](const juce::String& rawName) {
            const auto baseName = sanitiseBaseName(rawName, extension);
            if (baseName.isEmpty())
                return;

            const auto directory = getAppDocumentsDirectory().getChildFile(mobileSubdirectory);
            if (!directory.createDirectory().wasOk()) {
                AppLogger::error("[PlatformFileDialogs] Cannot create " + directory.getFullPathName());
                return;
            }
            onChosen(directory.getChildFile(baseName + extension));
        });
#else
    juce::ignoreUnused(mobileSubdirectory);

    auto chooser = std::make_shared<juce::FileChooser>(title, desktopInitialFile, "*" + extension);
    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [chooser, extension, onChosen = std::move(onChosen)](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File{})
            return;
        onChosen(withExtension(file, extension));
    });
#endif
}

void chooseFilesToOpen(const juce::String& title,
                       const juce::File& desktopInitialLocation,
                       const juce::String& wildcardFilter,
                       bool allowMultiple,
                       bool copyExternalFiles,
                       juce::Component* messageParent,
                       OpenFilesCallback onChosen)
{
    auto chooser = std::make_shared<juce::FileChooser>(title, desktopInitialLocation, wildcardFilter);
    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;
    if (allowMultiple)
        flags |= juce::FileBrowserComponent::canSelectMultipleItems;

    juce::Component::SafePointer<juce::Component> safeParent(messageParent);

    chooser->launchAsync(flags,
        [chooser, copyExternalFiles, safeParent, onChosen = std::move(onChosen)](const juce::FileChooser& fc) {
#if JUCE_IOS
            const auto documents = getAppDocumentsDirectory();
            const auto importDirectory = documents.getChildFile("Imported");
            juce::Array<juce::File> files;
            juce::StringArray failures;

            for (const auto& url : fc.getURLResults()) {
                const auto local = url.isLocalFile() ? url.getLocalFile() : juce::File{};

                if (local != juce::File{} && (isInside(local, documents) || !copyExternalFiles)) {
                    files.add(local);
                    continue;
                }

                const auto displayName = local != juce::File{} ? local.getFileName() : url.getFileName();
                const auto legalName = juce::File::createLegalFileName(displayName);
                const auto destination = importDirectory.getNonexistentChildFile(
                    juce::File(legalName).getFileNameWithoutExtension(),
                    juce::File(legalName).getFileExtension(),
                    false);

                if (importDirectory.createDirectory().wasOk() && copyUrlToFile(url, destination)) {
                    AppLogger::info("[PlatformFileDialogs] Copied " + displayName + " to " + destination.getFullPathName());
                    files.add(destination);
                } else {
                    AppLogger::error("[PlatformFileDialogs] Copy failed for " + url.toString(false));
                    failures.add(displayName);
                }
            }

            if (!failures.isEmpty() && safeParent != nullptr) {
                ConfirmDialogContent::showMessage(safeParent.getComponent(),
                                                  LOC(kImportFailed),
                                                  LOC_RAW("These files could not be copied into OpenTune:")
                                                      + "\n" + failures.joinIntoString("\n"));
            }
#else
            juce::ignoreUnused(copyExternalFiles, safeParent);
            const auto files = fc.getResults();
#endif
            if (!files.isEmpty())
                onChosen(files);
        });
}

} // namespace OpenTune::PlatformFileDialogs
