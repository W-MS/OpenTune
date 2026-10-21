#pragma once

#include <vector>

namespace OpenTune {

// One flush of host transport requests. Later commands overwrite earlier ones
// so a timeline drag collapses to the position under the cursor.
struct HostTransportBatch
{
    bool hasSeek = false;
    double seekSeconds = 0.0;
    bool seekIsCommit = false;
    bool restartIfPlaying = false;

    bool hasTransport = false;
    bool startPlayback = false;

    bool hasCycle = false;
    bool cycleEnabled = false;

    bool empty() const noexcept
    {
        return !hasSeek && !hasTransport && !hasCycle;
    }
};

// Logic ignores requestSetPlaybackPosition while the transport is running.
// A committed seek on that host has to stop, move, then start again.
inline bool logicCommitRestartsPlayback(bool hostIsLogic, bool hostIsPlaying) noexcept
{
    return hostIsLogic && hostIsPlaying;
}

class HostTransportCoalescer
{
public:
    void previewSeek(double seconds) noexcept
    {
        batch_.hasSeek = true;
        batch_.seekSeconds = seconds;
    }

    void commitSeek(double seconds, bool restartIfPlaying) noexcept
    {
        batch_.hasSeek = true;
        batch_.seekSeconds = seconds;
        batch_.seekIsCommit = true;
        batch_.restartIfPlaying = restartIfPlaying;
    }

    void requestStart() noexcept
    {
        batch_.hasTransport = true;
        batch_.startPlayback = true;
    }

    void requestStop() noexcept
    {
        batch_.hasTransport = true;
        batch_.startPlayback = false;
        batch_.restartIfPlaying = false;
    }

    void requestCycle(bool enabled) noexcept
    {
        batch_.hasCycle = true;
        batch_.cycleEnabled = enabled;
    }

    bool hasPending() const noexcept { return !batch_.empty(); }

    HostTransportBatch take() noexcept
    {
        const HostTransportBatch out = batch_;
        batch_ = {};
        return out;
    }

private:
    HostTransportBatch batch_{};
};

struct HostTransportCall
{
    enum class Kind
    {
        Stop,
        Seek,
        Start,
        Cycle
    };

    Kind kind = Kind::Stop;
    double seconds = 0.0;
    bool cycleEnabled = false;
};

// Host call order. Logic applies a seek only after playback has stopped,
// so a restart or an explicit stop is issued before the position change.
inline std::vector<HostTransportCall> planHostTransportCalls(const HostTransportBatch& batch)
{
    std::vector<HostTransportCall> calls;
    if (batch.empty())
        return calls;

    calls.reserve(4);
    const bool explicitStop = batch.hasTransport && !batch.startPlayback;
    const bool explicitStart = batch.hasTransport && batch.startPlayback;
    const bool restart = batch.hasSeek && batch.seekIsCommit && batch.restartIfPlaying && !explicitStop;
    const bool stopFirst = restart || (explicitStop && batch.hasSeek);

    if (stopFirst)
        calls.push_back({ HostTransportCall::Kind::Stop, 0.0, false });
    if (batch.hasSeek)
        calls.push_back({ HostTransportCall::Kind::Seek, batch.seekSeconds, false });
    if (!explicitStop && (restart || explicitStart))
        calls.push_back({ HostTransportCall::Kind::Start, 0.0, false });
    if (explicitStop && !stopFirst)
        calls.push_back({ HostTransportCall::Kind::Stop, 0.0, false });
    if (batch.hasCycle)
        calls.push_back({ HostTransportCall::Kind::Cycle, 0.0, batch.cycleEnabled });
    return calls;
}

} // namespace OpenTune
