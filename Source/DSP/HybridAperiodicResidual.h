#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace OpenTune {

struct HybridAperiodicResult
{
    std::vector<float> residual;
    std::vector<float> alignedAudio;
};

struct HybridRenderPayload
{
    std::vector<float> sourceAudio;
    std::vector<float> residual;
    std::vector<uint8_t> voicedSamples;
    int64_t globalSampleOffset{0};
    size_t synthesisOffsetSamples{0};
    size_t publishSampleCount{0};

    size_t retainedBytes() const noexcept;
};

HybridAperiodicResult extractAperiodicHigh(const float* audio,
                                           size_t audioSamples,
                                           const std::vector<float>& f0,
                                           const std::vector<uint8_t>& voiced,
                                           size_t firstGlobalFrame = 0,
                                           size_t totalFrameCount = 0);

std::vector<float> mixHybridRender(const std::vector<float>& synthesis,
                                   const HybridRenderPayload& payload);

} // namespace OpenTune
