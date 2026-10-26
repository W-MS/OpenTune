#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace OpenTune::PlatformFileDialogs {

/**
 * Desktop 使用系统原生对话框，直接返回用户选择的路径。
 *
 * iPadOS 的文档选择器只交回带安全作用域的 URL，App 不能在其旁边新建目录
 * （工程需要同级 *_Media 目录），后台线程也无法持有访问权限。因此 iPadOS 上：
 * - 保存/导出：提示输入文件名，目标固定在 App 的 Documents/<子目录> 下，
 *   通过 UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace 在“文件”App 中可见；
 * - 打开：copyExternalFiles=true 时以副本方式打开并移入 Documents/Imported，
 *   否则就地打开并按本地路径返回。
 */

using SaveTargetCallback = std::function<void(const juce::File&)>;
using OpenFilesCallback = std::function<void(const juce::Array<juce::File>&)>;

/** App 私有可见的文档根目录（iPadOS 为容器 Documents，桌面为用户 Documents/OpenTune）。 */
juce::File getAppDocumentsDirectory();

/**
 * 选择保存目标。extension 形如 ".wav"；返回的文件必定带该扩展名。
 * 用户取消时不调用 onChosen。
 */
void chooseSaveTarget(const juce::String& title,
                      const juce::File& desktopInitialFile,
                      const juce::String& extension,
                      const juce::String& mobileSubdirectory,
                      SaveTargetCallback onChosen);

/**
 * 选择要打开的文件。用户取消或没有可用文件时不调用 onChosen。
 * copyExternalFiles 仅影响 iPadOS：为 true 时所选文件复制进 Documents/Imported。
 */
void chooseFilesToOpen(const juce::String& title,
                       const juce::File& desktopInitialLocation,
                       const juce::String& wildcardFilter,
                       bool allowMultiple,
                       bool copyExternalFiles,
                       juce::Component* messageParent,
                       OpenFilesCallback onChosen);

} // namespace OpenTune::PlatformFileDialogs
