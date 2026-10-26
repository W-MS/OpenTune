#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace OpenTune::TouchInput {

/**
 * JUCE 在 iOS 上把手指、触控板/鼠标指针和悬停一律报告为 InputSourceType::touch，
 * 无法区分。iOS 实现在每个 UIWindow 上挂一个不拦截事件的手势识别器读取
 * UITouch.type（需要 Info.plist 的 UIApplicationSupportsIndirectInputEvents，
 * 否则指针点击也会被系统报告为手指），并用悬停识别器感知指针移动。
 * 桌面平台始终视为指针。
 */
enum class Kind
{
    Pointer,
    Finger,
    Pencil
};

#if JUCE_IOS
/** 开始观察（可重复调用）；新出现的窗口会被自动挂上观察器。 */
void startObserving();

/** 最近一次按下所用的输入类型。UIKit 先把触摸交给手势识别器，所以在 mouseDown 中读取即为本次按下。 */
Kind lastPressKind();

/** 最近的输入是否来自鼠标/触控板指针（按下或悬停）。 */
bool isPointerActive();
#else
inline void startObserving() {}
inline Kind lastPressKind() { return Kind::Pointer; }
inline bool isPointerActive() { return true; }
#endif

inline bool isFingerPress(const juce::MouseEvent& e)
{
    return e.source.isTouch() && lastPressKind() == Kind::Finger;
}

} // namespace OpenTune::TouchInput
