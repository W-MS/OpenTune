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

/** Coverage answer at one absolute host sample (see forEachUncoveredRun). */
struct CoverageProbeResult
{
    int64_t insideEnd = 0;  // exclusive end of the covering range, or absPos if uncovered
    int64_t nextStart = 0;  // start of the nearest covered range after absPos, or blockEnd when none
};

inline void probeSortedCoverageRanges(const HostSampleRange* ranges,
                                     int count,
                                     int64_t position,
                                     CoverageProbeResult& result) noexcept
{
    int firstAfter = 0;
    int end = count;
    while (firstAfter < end)
    {
        const int middle = firstAfter + (end - firstAfter) / 2;
        if (ranges[middle].start <= position)
            firstAfter = middle + 1;
        else
            end = middle;
    }

    if (firstAfter > 0)
    {
        const auto& previous = ranges[firstAfter - 1];
        const int64_t rangeEnd = previous.start + previous.count;
        if (position < rangeEnd)
            result.insideEnd = result.insideEnd > rangeEnd ? result.insideEnd : rangeEnd;
    }
    if (firstAfter < count && ranges[firstAfter].start < result.nextStart)
        result.nextStart = ranges[firstAfter].start;
}

namespace detail {
    inline int64_t minI64(int64_t a, int64_t b) noexcept { return a < b ? a : b; }
}

/** Streaming walk of ALL uncovered runs of one host block, in order, with no
 *  fixed range cap. No allocation, no locks — audio-thread safe.
 *
 *  probe(absPos) must return, for one absolute host sample:
 *    - insideEnd : exclusive end of the covered range containing absPos, or
 *                  absPos when the position is uncovered
 *    - nextStart : start of the nearest covered range after absPos, or absPos
 *                  when none
 *  Each uncovered run is reported exactly once via consume(offset, count); when
 *  the callback returns false the walk stops and the run count so far is returned. */
template <typename ProbeFn, typename ConsumeFn>
inline int forEachUncoveredRun(int64_t blockStart,
                               int blockSamples,
                               ProbeFn&& probe,
                               ConsumeFn&& consume) noexcept
{
    if (blockSamples <= 0)
        return 0;

    const int64_t blockEnd = blockStart + static_cast<int64_t>(blockSamples);
    int produced = 0;
    int cursor = 0;
    while (cursor < blockSamples)
    {
        const int64_t absolute = blockStart + static_cast<int64_t>(cursor);
        const auto coverage = probe(absolute);
        if (coverage.insideEnd > absolute)
        {
            // Covered: jump to the end of the covering range.
            cursor += static_cast<int>(detail::minI64(coverage.insideEnd, blockEnd) - absolute);
            continue;
        }
        int64_t run = detail::minI64(coverage.nextStart, blockEnd) - absolute;
        if (run <= 0)
            run = static_cast<int64_t>(blockSamples - cursor);  // no covered range ahead
        if (!consume(cursor, static_cast<int>(run)))
            return produced;
        ++produced;
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
