#pragma once

#include <cstdint>

namespace OpenTune {

// Where the plugin draws its edit playhead after a timeline gesture.
//
// The gesture always moves the in-plugin playhead so pitch editing can
// continue. A host that accepts the seek is followed once its transport
// arrives. A host that cannot move keeps the edit cursor until the DAW
// playhead itself moves somewhere else.
struct PendingSeekPolicy
{
    static constexpr double matchToleranceSeconds = 0.05;
    static constexpr double holdDeadlineSeconds = 0.85;

    struct Input
    {
        double pendingTimeSeconds = -1.0;
        double canonicalTimeSeconds = 0.0;
        double hostTimeAtGestureSeconds = 0.0;
        double issuedAtSeconds = 0.0;
        double nowSeconds = 0.0;
        std::uint64_t pendingEpoch = 0;
        std::uint64_t currentEpoch = 0;
        bool hostBound = false;
    };

    struct Decision
    {
        bool holdPending = false;
        bool clearPending = false;
        // The host did not land on the edit cursor. Keep showing it, and stop
        // waiting for a transport confirmation.
        bool detachFromHost = false;
    };

    static bool near(double a, double b) noexcept
    {
        const double delta = a - b;
        return delta <= matchToleranceSeconds && delta >= -matchToleranceSeconds;
    }

    static Decision evaluate(const Input& in) noexcept
    {
        Decision decision;
        if (!(in.pendingTimeSeconds >= 0.0))
            return decision;

        const bool epochAdvanced = in.currentEpoch != in.pendingEpoch;
        const bool hostAtEdit = epochAdvanced && near(in.canonicalTimeSeconds, in.pendingTimeSeconds);
        if (hostAtEdit)
        {
            decision.clearPending = true;
            return decision;
        }

        if (!in.hostBound)
        {
            const bool dawMoved = epochAdvanced
                && !near(in.canonicalTimeSeconds, in.hostTimeAtGestureSeconds)
                && !near(in.canonicalTimeSeconds, in.pendingTimeSeconds);
            if (dawMoved)
            {
                decision.clearPending = true;
                return decision;
            }
            decision.holdPending = true;
            return decision;
        }

        if ((in.nowSeconds - in.issuedAtSeconds) >= holdDeadlineSeconds)
        {
            decision.holdPending = true;
            decision.detachFromHost = true;
            return decision;
        }

        decision.holdPending = true;
        return decision;
    }
};

} // namespace OpenTune
