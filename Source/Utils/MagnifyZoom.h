#pragma once

#include <cmath>

namespace OpenTune {

struct MagnifyZoom
{
    double factor = 1.0;
    bool apply = false;
};

// JUCE reports a trackpad pinch through mouseMagnify as a per-event multiplier.
// 1 leaves the view unchanged. Values above 1 enlarge it. One event is one
// small step, so the factor is applied directly and only clamped against spikes.
inline MagnifyZoom magnifyScaleToZoom(float scaleFactor) noexcept
{
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return {};
    if (std::abs(scaleFactor - 1.0f) < 1.0e-4f)
        return {};

    constexpr float kMinFactor = 0.25f;
    constexpr float kMaxFactor = 4.0f;
    const float clamped = scaleFactor < kMinFactor
        ? kMinFactor
        : (scaleFactor > kMaxFactor ? kMaxFactor : scaleFactor);
    return { static_cast<double>(clamped), true };
}

} // namespace OpenTune
