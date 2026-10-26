#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include "IosDocumentPicker.h"
#include "AppLogger.h"

#include <juce_events/juce_events.h>

using PickedHandler = std::function<void(NSArray<NSURL*>*)>;

@interface OpenTuneDocumentPickerDelegate : NSObject <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate>
{
@public
    PickedHandler handler;
}
@end

@implementation OpenTuneDocumentPickerDelegate

- (void) finishWithURLs: (NSArray<NSURL*>*) urls
{
    if (! handler)
        return;

    auto pending = std::move (handler);
    handler = nullptr;
    pending (urls);
}

- (void) documentPicker: (UIDocumentPickerViewController*) controller didPickDocumentsAtURLs: (NSArray<NSURL*>*) urls
{
    [self finishWithURLs: urls];
}

- (void) documentPickerWasCancelled: (UIDocumentPickerViewController*) controller
{
    [self finishWithURLs: @[]];
}

- (void) presentationControllerDidDismiss: (UIPresentationController*) presentationController
{
    [self finishWithURLs: @[]];
}

@end

namespace OpenTune::IosDocumentPicker {

namespace {

char kDelegateKey = 0;

juce::String toJuce(NSString* s)
{
    return s != nil ? juce::String::fromUTF8([s UTF8String]) : juce::String();
}

NSString* toNS(const juce::String& s)
{
    return [NSString stringWithUTF8String: s.toRawUTF8()];
}

UIViewController* topViewController()
{
    UIWindow* window = nil;

    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
    {
        if (! [scene isKindOfClass: [UIWindowScene class]])
            continue;

        auto* windowScene = (UIWindowScene*) scene;
        if (windowScene.activationState != UISceneActivationStateForegroundActive)
            continue;

        window = windowScene.keyWindow;

        if (window == nil)
            for (UIWindow* candidate in windowScene.windows)
                if (candidate.rootViewController != nil && ! candidate.hidden)
                    window = candidate;

        if (window != nil)
            break;
    }

    auto* controller = window.rootViewController;

    while (controller.presentedViewController != nil && ! controller.presentedViewController.isBeingDismissed)
        controller = controller.presentedViewController;

    return controller;
}

bool hasAllowedExtension(const juce::File& file, const juce::StringArray& extensions)
{
    const auto extension = file.getFileExtension().trimCharactersAtStart(".");

    for (const auto& allowed : extensions)
        if (extension.equalsIgnoreCase(allowed))
            return true;

    return false;
}

Result collectResults(NSArray<NSURL*>* urls,
                      const juce::StringArray& extensions,
                      bool filterByExtension,
                      const juce::File& copyIntoDirectory)
{
    Result result;
    const bool copying = copyIntoDirectory != juce::File();

    if (copying && [urls count] > 0 && ! copyIntoDirectory.createDirectory().wasOk())
        AppLogger::error("[IosDocumentPicker] Cannot create " + copyIntoDirectory.getFullPathName());

    for (NSURL* url in urls)
    {
        const juce::File picked(toJuce(url.path));
        const auto displayName = picked.getFileName();

        if (filterByExtension && ! hasAllowedExtension(picked, extensions))
        {
            result.failures.add(displayName);
            continue;
        }

        if (! copying)
        {
            // 就地打开目录外的文件需要安全作用域访问；工程加载可能跨越多个异步步骤，故不主动结束访问。
            [url startAccessingSecurityScopedResource];
            result.files.add(picked);
            continue;
        }

        const auto legalName = juce::File::createLegalFileName(displayName);
        const auto destination = copyIntoDirectory.getNonexistentChildFile(
            juce::File(legalName).getFileNameWithoutExtension(),
            juce::File(legalName).getFileExtension(),
            false);
        auto* destinationURL = [NSURL fileURLWithPath: toNS(destination.getFullPathName())];

        NSError* error = nil;
        auto* fileManager = [NSFileManager defaultManager];
        bool ok = [fileManager moveItemAtURL: url toURL: destinationURL error: &error];

        if (! ok)
        {
            error = nil;
            const bool scoped = [url startAccessingSecurityScopedResource];
            ok = [fileManager copyItemAtURL: url toURL: destinationURL error: &error];
            if (scoped)
                [url stopAccessingSecurityScopedResource];
        }

        if (ok)
        {
            AppLogger::info("[IosDocumentPicker] Imported " + displayName + " -> " + destination.getFullPathName());
            result.files.add(destination);
        }
        else
        {
            AppLogger::error("[IosDocumentPicker] Import failed for " + displayName + ": "
                             + toJuce(error.localizedDescription));
            result.failures.add(displayName);
        }
    }

    return result;
}

} // namespace

void presentOpenPicker(const juce::StringArray& extensions,
                       bool allowMultiple,
                       const juce::File& copyIntoDirectory,
                       Callback onFinished)
{
    auto* types = [NSMutableArray<UTType*> array];
    bool filterByExtension = false;

    for (const auto& extension : extensions)
    {
        auto* type = [UTType typeWithFilenameExtension: toNS(extension)];

        if (type != nil && ! type.dynamic)
            [types addObject: type];
        else
            filterByExtension = true;
    }

    if ([types count] == 0 || filterByExtension)
        [types addObject: UTTypeData];

    auto* controller = topViewController();
    if (controller == nil)
    {
        AppLogger::error("[IosDocumentPicker] No view controller to present from");
        onFinished({});
        return;
    }

    const bool copying = copyIntoDirectory != juce::File();
    auto* picker = [[[UIDocumentPickerViewController alloc] initForOpeningContentTypes: types
                                                                                asCopy: copying] autorelease];
    picker.allowsMultipleSelection = allowMultiple;
    picker.shouldShowFileExtensions = YES;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;

    auto* delegate = [[[OpenTuneDocumentPickerDelegate alloc] init] autorelease];
    delegate->handler = [extensions, filterByExtension, copyIntoDirectory, onFinished = std::move(onFinished)]
        (NSArray<NSURL*>* urls)
    {
        auto result = collectResults(urls, extensions, filterByExtension, copyIntoDirectory);

        // 推迟到选择器退场之后，回调里弹出的 JUCE 对话框才能获得焦点。
        juce::MessageManager::callAsync([onFinished, result = std::move(result)] { onFinished(result); });
    };

    picker.delegate = delegate;
    picker.presentationController.delegate = delegate;
    objc_setAssociatedObject(picker, &kDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    [controller presentViewController: picker animated: YES completion: nil];
}

} // namespace OpenTune::IosDocumentPicker
