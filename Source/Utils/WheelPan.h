#pragma once

namespace OpenTune {

struct WheelPanPixels
{
    double x = 0.0;
    double y = 0.0;
};

// Trackpad deltas from JUCE on macOS are scrolling points multiplied by
// 0.5/256. Convert those back to points so a finger move pans one-to-one.
// Notched mouse wheels keep the user scroll-speed setting.
inline WheelPanPixels wheelEventToPixels(float deltaX,
                                         float deltaY,
                                         bool smooth,
                                         float notchScrollSpeed) noexcept
{
    const double scale = smooth ? (256.0 / 0.5) : static_cast<double>(notchScrollSpeed);
    return { static_cast<double>(deltaX) * scale, static_cast<double>(deltaY) * scale };
}

} // namespace OpenTune
