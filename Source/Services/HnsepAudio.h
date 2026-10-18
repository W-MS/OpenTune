#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace OpenTune {

struct HnsepChunkOutput
{
    int64_t startSample{0};
    std::vector<float> harmonic;
};

struct HnsepSilentGap
{
    int64_t startSample{0};
    int64_t endSampleExclusive{0};
};

// Match the optimized HNSEP preprocessing command: target 5s chunks, minimum
// 3s, and cuts only at hop-aligned samples inside detected silence. There is
// deliberately no maximum chunk size; absent eligible silence, keep the tail
// intact rather than cutting voiced audio arbitrarily.
inline std::vector<int64_t> buildHnsepChunkBoundaries(
    int64_t sampleCount,
    const std::vector<HnsepSilentGap>& silentGaps,
    int hopSize = 512,
    int64_t targetChunkSamples = 5 * 44100,
    int64_t minChunkSamples = 3 * 44100)
{
    if (sampleCount <= 0 || hopSize <= 0
        || targetChunkSamples <= 0 || minChunkSamples <= 0)
        return {};

    std::vector<int64_t> candidates;
    for (const auto& gap : silentGaps)
    {
        const int64_t gapStart = std::max<int64_t>(0, gap.startSample);
        const int64_t gapEnd = std::min(sampleCount, gap.endSampleExclusive);
        if (gapEnd <= gapStart)
            continue;
        const int64_t first = ((gapStart + hopSize - 1) / hopSize) * hopSize;
        const int64_t last = ((gapEnd - 1) / hopSize) * hopSize;
        for (int64_t candidate = first; candidate <= last && candidate < sampleCount;
             candidate += hopSize)
        {
            if (candidate > 0)
                candidates.push_back(candidate);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    std::vector<int64_t> boundaries{0};
    for (;;)
    {
        const int64_t start = boundaries.back();
        const int64_t minimumPosition = start + minChunkSamples;
        if (minimumPosition >= sampleCount)
            break;
        const auto firstEligible = std::lower_bound(
            candidates.begin(), candidates.end(), minimumPosition);
        if (firstEligible == candidates.end())
            break;

        const int64_t desired = start + targetChunkSamples;
        const auto insertion = std::lower_bound(firstEligible, candidates.end(), desired);
        int64_t cut = -1;
        if (insertion != candidates.end())
            cut = *insertion;
        if (insertion != firstEligible)
        {
            const int64_t previous = *(insertion - 1);
            if (cut < 0 || desired - previous <= cut - desired)
                cut = previous;
        }
        if (cut <= start || cut >= sampleCount)
            break;
        boundaries.push_back(cut);
    }

    boundaries.push_back(sampleCount);
    if (boundaries.size() > 2
        && boundaries.back() - boundaries[boundaries.size() - 2] < minChunkSamples)
        boundaries.erase(boundaries.end() - 2);
    return boundaries;
}

// Chunks overlap only at adjacent boundaries. Blend those samples linearly and
// reject gaps so every output sample remains tied to the dry-source timeline.
inline bool stitchHnsepChunkOutputs(size_t sampleCount,
                                    const std::vector<HnsepChunkOutput>& chunks,
                                    std::vector<float>& harmonic)
{
    harmonic.clear();
    if (sampleCount == 0 || chunks.empty())
        return false;

    harmonic.assign(sampleCount, 0.0f);
    size_t writtenEnd = 0;
    for (const auto& chunk : chunks)
    {
        if (chunk.startSample < 0 || chunk.harmonic.empty())
            return false;
        const size_t start = static_cast<size_t>(chunk.startSample);
        if (start > sampleCount || chunk.harmonic.size() > sampleCount - start
            || start > writtenEnd)
            return false;

        const size_t end = start + chunk.harmonic.size();
        const size_t overlapEnd = std::min(writtenEnd, end);
        const size_t overlapCount = overlapEnd - start;
        for (size_t i = 0; i < overlapCount; ++i)
        {
            const float newWeight = static_cast<float>(i + 1)
                / static_cast<float>(overlapCount + 1);
            const size_t sample = start + i;
            harmonic[sample] = harmonic[sample] * (1.0f - newWeight)
                + chunk.harmonic[i] * newWeight;
        }
        for (size_t i = overlapCount; i < chunk.harmonic.size(); ++i)
            harmonic[start + i] = chunk.harmonic[i];
        writtenEnd = std::max(writtenEnd, end);
    }

    if (writtenEnd != sampleCount)
    {
        harmonic.clear();
        return false;
    }
    return true;
}

inline bool makeHnsepResidual(const float* dry,
                              size_t sampleCount,
                              const std::vector<float>& harmonic,
                              std::vector<float>& residual)
{
    residual.clear();
    if (dry == nullptr || sampleCount != harmonic.size())
        return false;
    residual.resize(sampleCount);
    for (size_t i = 0; i < sampleCount; ++i)
        residual[i] = dry[i] - harmonic[i];
    return true;
}

inline bool makeHnsepResidual(const std::vector<float>& dry,
                              const std::vector<float>& harmonic,
                              std::vector<float>& residual)
{
    return makeHnsepResidual(dry.data(), dry.size(), harmonic, residual);
}

// The offset is always a dry-audio sample offset. Requests extending past the
// clip end are zero-padded for render context, never translated from F0 frames.
inline bool sliceHnsepSamples(const std::vector<float>& source,
                              int64_t sampleOffset,
                              size_t requestedCount,
                              std::vector<float>& slice)
{
    slice.clear();
    if (sampleOffset < 0 || static_cast<uint64_t>(sampleOffset) > source.size())
        return false;
    const size_t offset = static_cast<size_t>(sampleOffset);
    slice.assign(requestedCount, 0.0f);
    const size_t copyCount = std::min(requestedCount, source.size() - offset);
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset),
                copyCount, slice.begin());
    return true;
}

} // namespace OpenTune
