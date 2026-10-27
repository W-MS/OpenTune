#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace OpenTune {

/**
 * juce::PopupMenu 只在指针停留在条目上超过约 100 ms 时展开子菜单；手指轻点时
 * 按下即抬起，子菜单从不出现，只能长按。本监听器在触摸抬起时找到被点的菜单条目，
 * 通过其无障碍 showMenu 动作（JUCE 为带子菜单的条目提供）立即展开子菜单。
 */
class TouchSubmenuOpener final : private juce::MouseListener
{
public:
    TouchSubmenuOpener()
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }

    ~TouchSubmenuOpener() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }

private:
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!e.source.isTouch() || e.mouseWasDraggedSinceMouseDown())
            return;

        for (auto* c = e.originalComponent; c != nullptr; c = c->getParentComponent())
        {
            auto* handler = c->getAccessibilityHandler();
            if (handler == nullptr || handler->getRole() != juce::AccessibilityRole::menuItem)
                continue;

            if (!handler->getActions().contains(juce::AccessibilityActionType::showMenu))
                return;

            juce::Component::SafePointer<juce::Component> item(c);
            juce::MessageManager::callAsync([item] {
                if (item != nullptr)
                    if (auto* h = item->getAccessibilityHandler())
                        h->getActions().invoke(juce::AccessibilityActionType::showMenu);
            });
            return;
        }
    }
};

} // namespace OpenTune
