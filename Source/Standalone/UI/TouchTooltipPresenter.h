#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UIColors.h"

namespace OpenTune {

/**
 * juce::TooltipWindow 忽略触摸输入，iPad 上图标按钮的提示永远不会出现。
 * 本类监听宿主组件（含所有子组件）的触摸：Trigger::press 在按下时显示被按控件的
 * tooltip，Trigger::longPress 在原地按住约半秒后才显示（手指移动则取消）。
 * 标签在松手 2 秒后淡出。
 * 控件 properties 中 kTouchTextProperty 非空时优先显示它（可多行，用于触屏专用的用法说明）；
 * 设置 kSuppressProperty = true 可跳过该控件。
 */
class TouchTooltipPresenter final : private juce::MouseListener,
                                    private juce::Timer
{
public:
    static constexpr const char* kSuppressProperty = "touchTooltipSuppressed";
    static constexpr const char* kTouchTextProperty = "touchTooltipText";

    enum class Trigger
    {
        press,
        longPress
    };

    explicit TouchTooltipPresenter(juce::Component& host, Trigger trigger = Trigger::press)
        : host_(host), trigger_(trigger)
    {
        host_.addMouseListener(this, true);
    }

    ~TouchTooltipPresenter() override
    {
        host_.removeMouseListener(this);
    }

private:
    class Bubble final : public juce::Component
    {
    public:
        Bubble()
        {
            setInterceptsMouseClicks(false, false);
        }

        /** 按最大宽度排版文本，返回所需尺寸。 */
        juce::Point<int> layoutText(const juce::String& text, int maxWidth)
        {
            juce::AttributedString attributed;
            attributed.setJustification(juce::Justification::centred);
            attributed.append(text, UIColors::getLabelFont(15.0f), juce::Colours::white);

            layout_.createLayout(attributed, static_cast<float>(juce::jmax(40, maxWidth - kPaddingX * 2)));
            repaint();

            return { juce::roundToInt(std::ceil(layout_.getWidth())) + kPaddingX * 2,
                     juce::roundToInt(std::ceil(layout_.getHeight())) + kPaddingY * 2 };
        }

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(juce::Colour(0xee1c1f26));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
            layout_.draw(g, getLocalBounds().reduced(kPaddingX, kPaddingY).toFloat());
        }

    private:
        static constexpr int kPaddingX = 10;
        static constexpr int kPaddingY = 5;

        juce::TextLayout layout_;
    };

    static constexpr int kVisibleMs = 2000;
    static constexpr int kFadeMs = 250;
    static constexpr int kTickMs = 33;
    static constexpr int kLongPressMs = 500;
    static constexpr int kLongPressSlop = 10;
    static constexpr int kMaxBubbleWidth = 380;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.source.isTouch())
            return;

        juce::String text;
        auto* target = findTarget(e.originalComponent, text);
        if (target == nullptr)
            return;

        if (trigger_ == Trigger::press)
        {
            show(*target, text);
            return;
        }

        pendingTarget_ = target;
        pendingText_ = text;
        pendingMs_ = 0;
        startTimer(kTickMs);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (pendingTarget_ != nullptr && e.getDistanceFromDragStart() > kLongPressSlop)
            pendingTarget_ = nullptr;
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!e.source.isTouch())
            return;

        pendingTarget_ = nullptr;
        if (held_)
        {
            held_ = false;
            elapsedMs_ = 0;
        }
    }

    juce::Component* findTarget(juce::Component* origin, juce::String& text) const
    {
        for (auto* c = origin; c != nullptr && c != &host_; c = c->getParentComponent())
        {
            const auto& properties = c->getProperties();
            if (static_cast<bool>(properties.getWithDefault(kSuppressProperty, false)))
                return nullptr;

            text = properties[kTouchTextProperty].toString().trim();
            if (text.isEmpty())
                if (auto* client = dynamic_cast<juce::TooltipClient*>(c))
                    text = client->getTooltip().trim();

            if (text.isNotEmpty())
                return c;
        }
        return nullptr;
    }

    void show(juce::Component& target, const juce::String& text)
    {
        // 放进编辑器的直接子容器：弹出菜单也以它为父组件，这样标签才能排在菜单之下。
        juce::Component* parent = target.getTopLevelComponent();
        if (auto* editor = target.findParentComponentOfClass<juce::AudioProcessorEditor>())
        {
            parent = editor;
            for (auto* c = &target; c != nullptr && c != editor; c = c->getParentComponent())
                if (c->getParentComponent() == editor)
                    parent = c;
        }
        if (parent == nullptr)
            return;

        if (bubble_ == nullptr)
            bubble_ = std::make_unique<Bubble>();
        if (bubble_->getParentComponent() != parent)
            parent->addChildComponent(*bubble_);

        const auto size = bubble_->layoutText(text, juce::jmin(kMaxBubbleWidth, parent->getWidth() - 16));
        const auto targetArea = parent->getLocalArea(&target, target.getLocalBounds());

        // 按住时手指遮住控件下方，长按的标签优先放在控件上方。
        const bool above = trigger_ == Trigger::longPress && targetArea.getY() - 8 - size.y >= 8;
        const int centreY = above ? targetArea.getY() - 8 - size.y / 2
                                  : targetArea.getBottom() + 8 + size.y / 2;
        auto area = juce::Rectangle<int>(size.x, size.y).withCentre({ targetArea.getCentreX(), centreY });
        area = area.constrainedWithin(parent->getLocalBounds().reduced(8));

        bubble_->setBounds(area);
        bubble_->setAlpha(1.0f);
        bubble_->setVisible(true);
        bubble_->toFront(false);
        keepBelowPopups();

        held_ = true;
        elapsedMs_ = 0;
        fadeStartMs_ = -1;
        startTimer(kTickMs);
    }

    // 弹出菜单（以及其他模态浮层）进入模态状态；标签始终排在最底层的模态兄弟组件之下。
    void keepBelowPopups()
    {
        auto* parent = bubble_ != nullptr ? bubble_->getParentComponent() : nullptr;
        if (parent == nullptr)
            return;

        for (auto* sibling : parent->getChildren())
        {
            if (sibling != bubble_.get() && sibling->isCurrentlyModal(false))
            {
                bubble_->toBehind(sibling);
                return;
            }
        }
    }

    void timerCallback() override
    {
        if (pendingTarget_ != nullptr)
        {
            pendingMs_ += kTickMs;
            if (pendingMs_ >= kLongPressMs)
            {
                auto* target = pendingTarget_.getComponent();
                pendingTarget_ = nullptr;
                show(*target, pendingText_);
            }
            return;
        }

        if (bubble_ == nullptr || !bubble_->isVisible())
        {
            stopTimer();
            return;
        }

        keepBelowPopups();

        elapsedMs_ += kTickMs;
        if (elapsedMs_ < kVisibleMs || held_)
            return;

        if (fadeStartMs_ < 0)
            fadeStartMs_ = elapsedMs_;

        const float progress = static_cast<float>(elapsedMs_ - fadeStartMs_) / static_cast<float>(kFadeMs);
        if (progress >= 1.0f)
        {
            bubble_->setVisible(false);
            fadeStartMs_ = -1;
            stopTimer();
            return;
        }

        bubble_->setAlpha(1.0f - progress);
    }

    juce::Component& host_;
    const Trigger trigger_;
    std::unique_ptr<Bubble> bubble_;
    juce::Component::SafePointer<juce::Component> pendingTarget_;
    juce::String pendingText_;
    int pendingMs_ = 0;
    bool held_ = false;
    int elapsedMs_ = 0;
    int fadeStartMs_ = -1;
};

} // namespace OpenTune
