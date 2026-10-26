#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UIColors.h"

namespace OpenTune {

/**
 * juce::TooltipWindow 忽略触摸输入，iPad 上图标按钮的提示永远不会出现。
 * 本类监听宿主组件（含所有子组件）的触摸按下：显示被按控件的 tooltip，
 * 按下起 2 秒后（若仍按住则等到抬起）淡出。
 * 在控件的 properties 中设置 kSuppressProperty = true 可跳过该控件。
 */
class TouchTooltipPresenter final : private juce::MouseListener,
                                    private juce::Timer
{
public:
    static constexpr const char* kSuppressProperty = "touchTooltipSuppressed";

    explicit TouchTooltipPresenter(juce::Component& host) : host_(host)
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

        void setText(const juce::String& text)
        {
            text_ = text;
            repaint();
        }

        juce::Font getFont() const { return UIColors::getLabelFont(15.0f); }

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(juce::Colour(0xee1c1f26));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(getFont());
            g.drawFittedText(text_, getLocalBounds().reduced(kPaddingX, 4), juce::Justification::centred, 3);
        }

        static constexpr int kPaddingX = 10;

    private:
        juce::String text_;
    };

    static constexpr int kVisibleMs = 2000;
    static constexpr int kFadeMs = 250;
    static constexpr int kTickMs = 33;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.source.isTouch())
            return;

        juce::String text;
        for (auto* c = e.originalComponent; c != nullptr && c != &host_; c = c->getParentComponent())
        {
            if (static_cast<bool>(c->getProperties().getWithDefault(kSuppressProperty, false)))
                return;

            if (auto* client = dynamic_cast<juce::TooltipClient*>(c))
            {
                text = client->getTooltip().trim();
                if (text.isNotEmpty())
                {
                    show(*c, text);
                    return;
                }
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.source.isTouch())
            held_ = false;
    }

    void show(juce::Component& target, const juce::String& text)
    {
        juce::Component* parent = target.findParentComponentOfClass<juce::AudioProcessorEditor>();
        if (parent == nullptr)
            parent = target.getTopLevelComponent();
        if (parent == nullptr)
            return;

        if (bubble_ == nullptr)
            bubble_ = std::make_unique<Bubble>();
        if (bubble_->getParentComponent() != parent)
            parent->addChildComponent(*bubble_);

        bubble_->setText(text);

        const auto font = bubble_->getFont();
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text.upToFirstOccurrenceOf("\n", false, false), 0.0f, 0.0f);
        const int lines = juce::jlimit(1, 3, juce::StringArray::fromLines(text).size());
        const int width = juce::jmin(parent->getWidth() - 16,
                                     juce::roundToInt(glyphs.getBoundingBox(0, -1, true).getWidth()) + Bubble::kPaddingX * 2 + 4);
        const int height = juce::roundToInt(font.getHeight() * static_cast<float>(lines)) + 10;

        const auto targetArea = parent->getLocalArea(&target, target.getLocalBounds());
        auto area = juce::Rectangle<int>(width, height)
                        .withCentre({ targetArea.getCentreX(), targetArea.getBottom() + 8 + height / 2 });
        area = area.constrainedWithin(parent->getLocalBounds().reduced(8));

        bubble_->setBounds(area);
        bubble_->setAlpha(1.0f);
        bubble_->setVisible(true);
        bubble_->toFront(false);

        held_ = true;
        elapsedMs_ = 0;
        fadeStartMs_ = -1;
        startTimer(kTickMs);
    }

    void timerCallback() override
    {
        if (bubble_ == nullptr)
        {
            stopTimer();
            return;
        }

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
    std::unique_ptr<Bubble> bubble_;
    bool held_ = false;
    int elapsedMs_ = 0;
    int fadeStartMs_ = -1;
};

} // namespace OpenTune
