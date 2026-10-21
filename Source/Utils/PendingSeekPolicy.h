#pragma once

#include <cstdint>

namespace OpenTune {

// Optimistic playhead hold after a host seek request.
//
// The host is allowed to delay or quantize the request. A new audio block,
// or the host still reporting "playing", is not evidence that the seek landed.
// Hold the requested time until a later host observation is close to it, or
// until the deadline expires and the UI snaps back to the host position.
struct PendingSeekPolicy
{
    static constexpr double matchToleranceSeconds = 0.05;
    static constexpr double holdDeadlineSeconds = 0.85;

    struct Input
    {
        double pendingTimeSeconds = -1.0;
        double canonicalTimeSeconds = 0.0;
        double issuedAtSeconds = 0.0;
        double nowSeconds = 0.0;
        std::uint64_t pendingEpoch = 0;
        std::uint64_t currentEpoch = 0;
    };

    struct Decision
    {
        bool holdPending = false;
        bool clearPending = false;
    };

    static Decision evaluate(const Input& in) noexcept
    {
        Decision decision;
        if (!(in.pendingTimeSeconds >= 0.0))
            return decision;

        const bool epochAdvanced = in.currentEpoch != in.pendingEpoch;
        const double delta = in.canonicalTimeSeconds - in.pendingTimeSeconds;
        const bool hostMatched = epochAdvanced
            && delta <= matchToleranceSeconds
            && delta >= -matchToleranceSeconds;
        const bool expired = (in.nowSeconds - in.issuedAtSeconds) >= holdDeadlineSeconds;
        if (hostMatched || expired)
        {
            decision.clearPending = true;
            return decision;
        }

        decision.holdPending = true;
        return decision;
    }
};

} // namespace OpenTune
