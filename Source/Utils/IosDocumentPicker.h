#pragma once

#include <juce_core/juce_core.h>
#include <functional>

#if JUCE_IOS
namespace OpenTune::IosDocumentPicker {

struct Result
{
    juce::Array<juce::File> files;
    juce::StringArray failures;
};

using Callback = std::function<void(const Result&)>;

/**
 * 直接呈现 UIDocumentPickerViewController（不经 juce::FileChooser：后者在
 * NSFileCoordinator 报错时既不回调也不提示，导入会卡在选择界面）。
 *
 * extensions 形如 "wav"（无点）；系统无法识别的扩展名退化为 public.data 并在选择后按扩展名过滤。
 * copyIntoDirectory 非空时以副本方式打开，系统交回的临时副本被移入该目录；
 * 为空时就地打开，目录外的文件保持安全作用域访问直到进程结束。
 * 取消时以空结果回调。回调总在消息线程上执行。
 */
void presentOpenPicker(const juce::StringArray& extensions,
                       bool allowMultiple,
                       const juce::File& copyIntoDirectory,
                       Callback onFinished);

/** 回调参数为最终保存位置；用户取消时为空 File。 */
using ExportCallback = std::function<void(const juce::File& destination)>;

/**
 * 以“移动”方式把 file 交给系统导出面板，由用户在“文件”中选择保存位置。
 * 成功时原文件被系统移走；取消时原文件保持不动。回调总在消息线程上执行。
 */
void presentExportPicker(const juce::File& file, ExportCallback onFinished);

} // namespace OpenTune::IosDocumentPicker
#endif
