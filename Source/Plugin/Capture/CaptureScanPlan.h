#pragma once

#include <cstdint>

namespace OpenTune::Capture {

struct HostSampleRange
{
    int64_t start = 0;
    int64_t count = 0;
};

struct BlockSampleRun
{
    int blockOffset = 0;
    int sampleCount = 0;
};

// Sub-ranges of one host block that are not already inside a scanned segment.
// Audio-thread safe: no allocation. A zero-length range is ignored.
inline int collectUncoveredRuns(int64_t blockStart,
                                int blockSamples,
                                const HostSampleRange* covered,
                                int coveredCount,
                                BlockSampleRun* out,
                                int maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0 || blockSamples <= 0)
        return 0;

    int produced = 0;
    int cursor = 0;
    while (cursor < blockSamples && produced < maxOut)
    {
        const int64_t absolute = blockStart + static_cast<int64_t>(cursor);
        int64_t run = static_cast<int64_t>(blockSamples - cursor);
        bool inside = false;
        for (int i = 0; i < coveredCount; ++i)
        {
            const int64_t start = covered[i].start;
            const int64_t count = covered[i].count;
            if (count <= 0)
                continue;
            const int64_t end = start + count;
            if (absolute >= start && absolute < end)
            {
                inside = true;
                const int64_t untilEnd = end - absolute;
                if (untilEnd < run)
                    run = untilEnd;
            }
            else if (start > absolute)
            {
                const int64_t untilStart = start - absolute;
                if (untilStart < run)
                    run = untilStart;
            }
        }
        if (run <= 0)
            break;
        if (!inside)
        {
            out[produced].blockOffset = cursor;
            out[produced].sampleCount = static_cast<int>(run);
            ++produced;
        }
        cursor += static_cast<int>(run);
    }
    return produced;
}

enum class ScanTransportAction
{
    Hold,
    Arm,
    Stop
};

struct ScanTransportInput
{
    bool scanArmed = false;
    bool hostPlaying = false;
    bool positionCovered = false;
    bool capturing = false;
    bool pending = false;
};

// Armed playback of an unscanned sample starts a capture. Already scanned
// audio is left alone. Stopping the DAW, or turning scan off, ends the take.
inline ScanTransportAction decideScanTransport(const ScanTransportInput& in) noexcept
{
    if (!in.scanArmed || !in.hostPlaying)
        return in.capturing ? ScanTransportAction::Stop : ScanTransportAction::Hold;
    if (in.capturing || in.pending || in.positionCovered)
        return ScanTransportAction::Hold;
    return ScanTransportAction::Arm;
}

} // namespace OpenTune::Capture
