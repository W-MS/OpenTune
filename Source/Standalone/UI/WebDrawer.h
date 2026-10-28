#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "UIColors.h"
#include "../../Utils/LocalizationManager.h"

#if JUCE_IOS
#if !JUCE_WEB_BROWSER
 #error "WebDrawer requires JUCE_WEB_BROWSER=1 on iOS"
#endif

namespace OpenTune {

/**
 * iPad 内置网页抽屉：从右侧滑出，内嵌 WKWebView 打开用户手册，
 * 点击关闭按钮或抽屉外的遮罩随时关闭。铺满父组件，未打开时隐藏。
 */
class WebDrawer : public juce::Component
{
public:
    WebDrawer()
    {
        setAlwaysOnTop(true);

        styleButton(backButton_, false);
        styleButton(closeButton_, true);
        backButton_.onClick = [this] { browser_.goBack(); };
        closeButton_.onClick = [this] { close(); };

        panel_.addAndMakeVisible(backButton_);
        panel_.addAndMakeVisible(closeButton_);
        panel_.addAndMakeVisible(browser_);
        addAndMakeVisible(panel_);
        setVisible(false);
    }

    void open(const juce::String& url, const juce::String& title)
    {
        title_ = title;
        backButton_.setButtonText(LOC_RAW("Back"));
        closeButton_.setButtonText(LOC(kClose));

        ++closeToken_;
        auto& animator = juce::Desktop::getInstance().getAnimator();
        animator.cancelAnimation(&panel_, false);

        setVisible(true);
        toFront(false);
        panel_.setBounds(panelBounds().withX(getWidth()));
        layoutPanel();
        browser_.goToURL(url);
        animator.animateComponent(&panel_, panelBounds(), 1.0f, kAnimationMs, false, 1.0, 0.0);
        repaint();
    }

    void close()
    {
        if (!isVisible())
            return;

        const int token = ++closeToken_;
        auto& animator = juce::Desktop::getInstance().getAnimator();
        animator.animateComponent(&panel_, panelBounds().withX(getWidth()), 1.0f, kAnimationMs, false, 1.0, 0.0);
        juce::Timer::callAfterDelay(kAnimationMs + 20, [safeThis = juce::Component::SafePointer<WebDrawer>(this), token] {
            if (safeThis == nullptr || safeThis->closeToken_ != token)
                return;
            safeThis->setVisible(false);
            safeThis->browser_.goToURL("about:blank");
        });
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.45f));
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!panel_.getBounds().contains(e.getPosition()))
            close();
    }

    void resized() override
    {
        if (!juce::Desktop::getInstance().getAnimator().isAnimating(&panel_))
            panel_.setBounds(panelBounds());
        layoutPanel();
    }

private:
    static constexpr int kAnimationMs = 220;
    static constexpr int kHeaderHeight = 52;

    class Panel : public juce::Component
    {
    public:
        explicit Panel(WebDrawer& owner) : owner_(owner) {}

        void paint(juce::Graphics& g) override
        {
            g.fillAll(UIColors::backgroundDark);
            g.setColour(UIColors::panelBorder);
            g.fillRect(0, 0, 1, getHeight());
            g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));

            g.setColour(UIColors::textPrimary);
            g.setFont(UIColors::getHeaderFont(18.0f));
            g.drawText(owner_.title_, owner_.titleArea_, juce::Justification::centred, true);
        }

        void resized() override { owner_.layoutPanel(); }

    private:
        WebDrawer& owner_;
    };

    class Browser : public juce::WebBrowserComponent
    {
    public:
        Browser() : juce::WebBrowserComponent(juce::WebBrowserComponent::Options{}) {}

        bool pageAboutToLoad(const juce::String& url) override
        {
            if (url.startsWithIgnoreCase("mailto:") || url.startsWithIgnoreCase("tel:"))
            {
                juce::URL(url).launchInDefaultBrowser();
                return false;
            }
            return true;
        }

        // target="_blank" 链接在抽屉内打开，而不是跳出应用。
        void newWindowAttemptingToLoad(const juce::String& url) override { goToURL(url); }
    };

    static void styleButton(juce::TextButton& button, bool accent)
    {
        button.setColour(juce::TextButton::buttonColourId, accent ? UIColors::accent : UIColors::backgroundLight);
        button.setColour(juce::TextButton::textColourOffId, UIColors::textPrimary);
        button.setColour(juce::TextButton::textColourOnId, UIColors::textPrimary);
    }

    juce::Rectangle<int> panelBounds() const
    {
        const int width = juce::jlimit(juce::jmin(360, getWidth()), getWidth(),
                                       juce::jmax(720, getWidth() * 3 / 5));
        return { getWidth() - width, 0, width, getHeight() };
    }

    void layoutPanel()
    {
        auto bounds = panel_.getLocalBounds();
        auto header = bounds.removeFromTop(kHeaderHeight).reduced(10, 8);
        const int buttonWidth = 88;
        backButton_.setBounds(header.removeFromLeft(buttonWidth));
        closeButton_.setBounds(header.removeFromRight(buttonWidth));
        titleArea_ = header.reduced(8, 0);
        browser_.setBounds(bounds);
    }

    juce::String title_;
    juce::Rectangle<int> titleArea_;
    int closeToken_ = 0;
    Panel panel_{ *this };
    juce::TextButton backButton_;
    juce::TextButton closeButton_;
    Browser browser_;
};

} // namespace OpenTune
#endif
