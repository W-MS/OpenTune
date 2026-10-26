#import <UIKit/UIKit.h>

#include "TouchInput.h"

namespace {

OpenTune::TouchInput::Kind gLastPressKind = OpenTune::TouchInput::Kind::Finger;
bool gPointerActive = false;

void recordTouchType(UITouchType type)
{
    using OpenTune::TouchInput::Kind;

    switch (type)
    {
        case UITouchTypeDirect:
            gLastPressKind = Kind::Finger;
            gPointerActive = false;
            break;
        case UITouchTypePencil:
            gLastPressKind = Kind::Pencil;
            gPointerActive = false;
            break;
        default:
            gLastPressKind = Kind::Pointer;
            gPointerActive = true;
            break;
    }
}

} // namespace

// 只观察、从不识别：cancelsTouchesInView/delays 均关闭，事件照常送达 JUCE 的视图。
// 触摸序列结束时仍处于 Possible 状态的识别器由 UIKit 自动复位。
@interface OpenTuneTouchTypeObserver : UIGestureRecognizer <UIGestureRecognizerDelegate>
@end

@implementation OpenTuneTouchTypeObserver

- (instancetype) init
{
    self = [super initWithTarget: nil action: nil];

    if (self != nil)
    {
        self.cancelsTouchesInView = NO;
        self.delaysTouchesBegan = NO;
        self.delaysTouchesEnded = NO;
        self.delegate = self;
    }

    return self;
}

- (void) touchesBegan: (NSSet<UITouch*>*) touches withEvent: (UIEvent*) event
{
    for (UITouch* touch in touches)
        recordTouchType (touch.type);
}

- (void) touchesMoved: (NSSet<UITouch*>*) touches withEvent: (UIEvent*) event {}
- (void) touchesEnded: (NSSet<UITouch*>*) touches withEvent: (UIEvent*) event {}
- (void) touchesCancelled: (NSSet<UITouch*>*) touches withEvent: (UIEvent*) event {}

- (BOOL) gestureRecognizer: (UIGestureRecognizer*) gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer: (UIGestureRecognizer*) other
{
    return YES;
}

@end

@interface OpenTuneHoverObserver : NSObject <UIGestureRecognizerDelegate>
- (void) onHover: (UIHoverGestureRecognizer*) recognizer;
@end

@implementation OpenTuneHoverObserver

- (void) onHover: (UIHoverGestureRecognizer*) recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateBegan:
        case UIGestureRecognizerStateChanged:
            gPointerActive = true;
            break;
        default:
            break;
    }
}

- (BOOL) gestureRecognizer: (UIGestureRecognizer*) gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer: (UIGestureRecognizer*) other
{
    return YES;
}

@end

namespace OpenTune::TouchInput {

namespace {

OpenTuneHoverObserver* sharedHoverObserver()
{
    static OpenTuneHoverObserver* observer = [[OpenTuneHoverObserver alloc] init];
    return observer;
}

void installOn(UIWindow* window)
{
    for (UIGestureRecognizer* recognizer in window.gestureRecognizers)
        if ([recognizer isKindOfClass: [OpenTuneTouchTypeObserver class]])
            return;

    [window addGestureRecognizer: [[[OpenTuneTouchTypeObserver alloc] init] autorelease]];

    auto* hover = [[[UIHoverGestureRecognizer alloc] initWithTarget: sharedHoverObserver()
                                                             action: @selector (onHover:)] autorelease];
    hover.cancelsTouchesInView = NO;
    hover.delegate = sharedHoverObserver();
    [window addGestureRecognizer: hover];
}

void installOnAllWindows()
{
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
        if ([scene isKindOfClass: [UIWindowScene class]])
            for (UIWindow* window in ((UIWindowScene*) scene).windows)
                installOn (window);
}

// JUCE 的弹出菜单、对话框等各自创建 UIWindow，定时补挂观察器。
class WindowWatcher final : private juce::Timer,
                            private juce::DeletedAtShutdown
{
public:
    static void ensureRunning()
    {
        if (instance == nullptr)
            instance = new WindowWatcher();
    }

    ~WindowWatcher() override
    {
        stopTimer();
        instance = nullptr;
    }

private:
    WindowWatcher()
    {
        installOnAllWindows();
        startTimer (250);
    }

    void timerCallback() override { installOnAllWindows(); }

    static inline WindowWatcher* instance = nullptr;
};

} // namespace

void startObserving()
{
    WindowWatcher::ensureRunning();
}

Kind lastPressKind()
{
    return gLastPressKind;
}

bool isPointerActive()
{
    return gPointerActive;
}

} // namespace OpenTune::TouchInput
