#include "../Source/DSP/HybridAperiodicResidual.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

float rms(const std::vector<float>& audio)
{
    double energy = 0.0;
    for (const auto sample : audio)
        energy += static_cast<double>(sample) * sample;
    return audio.empty() ? 0.0f : static_cast<float>(std::sqrt(energy / audio.size()));
}

void testResidualAlignmentAndUv()
{
    constexpr int frameCount = 3;
    std::vector<float> audio(2048, 0.5f);
    const auto result = OpenTune::extractAperiodicHigh(
        audio.data(), audio.size(), std::vector<float>(frameCount, 220.0f),
        std::vector<uint8_t>(frameCount, 0));
    expect(result.alignedAudio.size() == frameCount * 512u,
           "aligned audio should have one hop per input frame");
    expect(result.residual.size() == frameCount * 512u,
           "residual should have one hop per input frame");
    expect(std::all_of(result.residual.begin(), result.residual.end(),
                       [](float value) { return value == 0.0f; }),
           "unvoiced residual samples should be zero");
    expect(std::all_of(result.alignedAudio.begin(), result.alignedAudio.end(),
                       [](float value) { return std::isfinite(value); }),
           "aligned audio should be finite");
}

void testPeriodicAndAperiodicHighBand()
{
    constexpr int frameCount = 40;
    constexpr int sampleCount = frameCount * 512;
    constexpr float pi = 3.14159265358979323846f;
    std::vector<float> periodic(sampleCount);
    std::vector<float> aperiodic(sampleCount);
    uint32_t random = 0x12345678u;
    for (int i = 0; i < sampleCount; ++i)
    {
        periodic[static_cast<size_t>(i)] = std::sin(2.0f * pi * 12000.0f * i / 44100.0f);
        random = random * 1664525u + 1013904223u;
        aperiodic[static_cast<size_t>(i)] = static_cast<float>(random >> 8) / 8388608.0f - 1.0f;
    }
    const std::vector<float> f0(frameCount, 750.0f);
    const std::vector<uint8_t> voiced(frameCount, 1);
    const auto periodicResult = OpenTune::extractAperiodicHigh(
        periodic.data(), periodic.size(), f0, voiced);
    const auto aperiodicResult = OpenTune::extractAperiodicHigh(
        aperiodic.data(), aperiodic.size(), f0, voiced);

    expect(rms(periodicResult.residual) < rms(aperiodicResult.residual),
           "harmonic high-band energy should be removed more strongly than aperiodic energy");
    expect(std::all_of(aperiodicResult.residual.begin(), aperiodicResult.residual.end(),
                       [](float value) { return std::isfinite(value); }),
           "aperiodic residual should remain finite");
}

void testGlobalFrameAlignedContext()
{
    constexpr size_t frameCount = 32;
    std::vector<float> audio(frameCount * 512);
    for (size_t i = 0; i < audio.size(); ++i)
        audio[i] = std::sin(2.0f * 3.14159265358979323846f * 9000.0f
                            * static_cast<float>(i) / 44100.0f);
    const std::vector<float> f0(frameCount, 250.0f);
    const std::vector<uint8_t> voiced(frameCount, 1);
    const auto full = OpenTune::extractAperiodicHigh(
        audio.data(), audio.size(), f0, voiced, 0, frameCount);

    constexpr size_t firstFrame = 8;
    constexpr size_t contextFrames = 16;
    const std::vector<float> contextF0(f0.begin() + firstFrame,
                                       f0.begin() + firstFrame + contextFrames);
    const std::vector<uint8_t> contextVoiced(voiced.begin() + firstFrame,
                                             voiced.begin() + firstFrame + contextFrames);
    const auto context = OpenTune::extractAperiodicHigh(
        audio.data(), audio.size(), contextF0, contextVoiced,
        firstFrame, frameCount);
    const size_t crop = 4 * 512;
    bool aligned = context.alignedAudio.size() == contextFrames * 512;
    bool residualMatches = aligned;
    for (size_t i = 0; aligned && i < context.alignedAudio.size(); ++i)
        aligned = context.alignedAudio[i] == full.alignedAudio[firstFrame * 512 + i];
    for (size_t i = crop; residualMatches && i + crop < context.residual.size(); ++i)
        residualMatches = std::abs(context.residual[i]
            - full.residual[firstFrame * 512 + i]) < 1.0e-5f;
    expect(aligned, "expanded source audio should retain its global hop alignment");
    expect(residualMatches, "internal residual frames should match full-clip extraction");
}

OpenTune::HybridRenderPayload makeMixPayload(size_t sampleCount,
                                             uint8_t initialVoiced,
                                             size_t publishOffset = 0,
                                             size_t publishCount = 0)
{
    OpenTune::HybridRenderPayload payload;
    payload.sourceAudio.assign(sampleCount, 1.0f);
    payload.residual.assign(sampleCount, 0.0f);
    payload.voicedSamples.assign(sampleCount, initialVoiced);
    payload.synthesisOffsetSamples = publishOffset;
    payload.publishSampleCount = publishCount == 0 ? sampleCount - publishOffset : publishCount;
    return payload;
}

void testUvSideCandidateCentersAndFadeDirections()
{
    auto entering = makeMixPayload(2048, 1);
    std::fill(entering.voicedSamples.begin() + 1024,
              entering.voicedSamples.end(), 0);
    const auto intoUv = OpenTune::mixHybridRender(
        std::vector<float>(entering.sourceAudio.size(), 0.0f), entering);
    expect(intoUv.size() == entering.sourceAudio.size(),
           "voiced-to-UV mix should preserve the output length");
    if (intoUv.size() == entering.sourceAudio.size())
    {
        expect(intoUv[1024] > 0.49f && intoUv[1024] < 0.51f,
               "the entering fade center should be the first UV-side candidate");
        expect(intoUv[913] == 0.0f && intoUv[1134] == 1.0f,
               "voiced-to-UV input weight should rise from zero to one");
    }

    auto exiting = makeMixPayload(3072, 0);
    std::fill(exiting.voicedSamples.begin() + 2048,
              exiting.voicedSamples.end(), 1);
    const auto outOfUv = OpenTune::mixHybridRender(
        std::vector<float>(exiting.sourceAudio.size(), 0.0f), exiting);
    expect(outOfUv.size() == exiting.sourceAudio.size(),
           "UV-to-voiced mix should preserve the output length");
    if (outOfUv.size() == exiting.sourceAudio.size())
    {
        expect(outOfUv[1536] > 0.49f && outOfUv[1536] < 0.51f,
               "the exiting fade center should remain on the UV side");
        expect(outOfUv[1426] == 1.0f && outOfUv[1646] == 0.0f,
               "UV-to-voiced input weight should fall from one to zero");
    }
}

void testShortUvTriangularMask()
{
    auto payload = makeMixPayload(1600, 1);
    constexpr size_t uvStart = 700;
    constexpr size_t uvEnd = 900;
    std::fill(payload.voicedSamples.begin() + uvStart,
              payload.voicedSamples.begin() + uvEnd, 0);

    const auto mixed = OpenTune::mixHybridRender(
        std::vector<float>(payload.sourceAudio.size(), 0.0f), payload);
    expect(mixed.size() == payload.sourceAudio.size(),
           "short UV mix should preserve the output length");
    if (mixed.size() != payload.sourceAudio.size())
        return;

    expect(mixed[uvStart - 1] == 0.0f && mixed[uvStart] < 0.02f,
           "short UV triangular input weight should start at the voiced edge");
    expect(mixed[uvStart + 99] > 0.99f && mixed[uvStart + 100] > 0.99f,
           "short UV triangular input weight should peak at the run center");
    expect(mixed[uvEnd - 1] < 0.02f && mixed[uvEnd] == 0.0f,
           "short UV triangular input weight should return to the voiced edge");
}

void testVoicedSynthesisAndResidualMix()
{
    auto payload = makeMixPayload(600, 1);
    std::fill(payload.residual.begin(), payload.residual.end(), 2.0f);
    const auto mixed = OpenTune::mixHybridRender(
        std::vector<float>(payload.sourceAudio.size(), 1.0f), payload);
    expect(mixed.size() == payload.sourceAudio.size(),
           "voiced-only mix should preserve the output length");
    if (mixed.size() == payload.sourceAudio.size())
        expect(std::all_of(mixed.begin(), mixed.end(),
                           [](float sample) { return sample == 3.0f; }),
               "voiced samples should retain synthesis plus the extracted residual");
}

void testShortAdjacentVoicedFade()
{
    auto payload = makeMixPayload(3072, 0);
    constexpr size_t transition = 2048;
    std::fill(payload.voicedSamples.begin() + transition,
              payload.voicedSamples.begin() + transition + 100, 1);

    const auto mixed = OpenTune::mixHybridRender(
        std::vector<float>(payload.sourceAudio.size(), 0.0f), payload);
    expect(mixed.size() == payload.sourceAudio.size(),
           "short voiced-boundary mix should preserve the output length");
    if (mixed.size() == payload.sourceAudio.size())
    {
        expect(mixed[1486] == 1.0f && mixed[1536] > 0.49f && mixed[1536] < 0.51f,
               "a voiced region shorter than 220 samples should limit the fade length");
        expect(mixed[1586] == 0.0f,
               "the shortened UV-to-voiced fade should finish within its 100-sample length");
    }
}

void testTransientPreservation()
{
    auto payload = makeMixPayload(2048, 1);
    std::fill(payload.voicedSamples.begin() + 1024,
              payload.voicedSamples.end(), 0);
    constexpr size_t transientSample = 1070;
    payload.sourceAudio[transientSample] = 7.0f;

    const auto mixed = OpenTune::mixHybridRender(
        std::vector<float>(payload.sourceAudio.size(), 0.0f), payload);
    expect(mixed.size() == payload.sourceAudio.size(),
           "transient mix should preserve the output length");
    if (mixed.size() == payload.sourceAudio.size())
        expect(mixed[transientSample] == payload.sourceAudio[transientSample],
                "detected UV transient samples should remain entirely on the input branch");
}

void testTransientFallbackFadeStaysBesideTransient()
{
    constexpr size_t sampleCount = 4096;
    auto payload = makeMixPayload(sampleCount, 1);
    constexpr size_t uvStart = 1536;
    std::fill(payload.voicedSamples.begin() + uvStart,
              payload.voicedSamples.end(), 0);

    uint32_t random = 0x89abcdefu;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        random = random * 1664525u + 1013904223u;
        float noise = static_cast<float>(random >> 8) / 8388608.0f - 1.0f;
        if (std::abs(noise) < 0.1f)
            noise = noise < 0.0f ? -0.1f : 0.1f;
        const size_t attackBlock = i < 1024 ? 0 : std::min<size_t>((i - 1024) / 128, 10);
        payload.sourceAudio[i] = noise * 0.0001f * static_cast<float>(1u << attackBlock);
    }

    const auto mixed = OpenTune::mixHybridRender(
        std::vector<float>(sampleCount, 0.0f), payload);
    expect(mixed.size() == sampleCount,
           "transient fallback mix should preserve the output length");
    if (mixed.size() != sampleCount)
        return;

    bool transientSamplesPreserved = true;
    for (size_t i = 1700; i < 2200; ++i)
        transientSamplesPreserved = transientSamplesPreserved
            && mixed[i] == payload.sourceAudio[i];
    expect(transientSamplesPreserved,
           "transient samples spanning the candidate area should remain source samples");

    float previousWeight = mixed[0] / payload.sourceAudio[0];
    bool smooth = true;
    for (size_t i = 1; i < sampleCount; ++i)
    {
        const float weight = mixed[i] / payload.sourceAudio[i];
        smooth = smooth && std::abs(weight - previousWeight) < 0.02f;
        previousWeight = weight;
    }
    expect(smooth,
           "fallback input weights should vary smoothly on both sides of the transient");
}

void testTransientFluxThresholdSemantics()
{
    constexpr size_t sampleCount = 3072;
    constexpr size_t attackStart = 1000;
    constexpr size_t attackEnd = 1080;
    constexpr size_t attackSample = 1040;
    const auto makeAttack = [=](float amplitude) {
        auto payload = makeMixPayload(sampleCount, 1);
        std::fill(payload.voicedSamples.begin() + 1024,
                  payload.voicedSamples.end(), 0);
        std::fill(payload.sourceAudio.begin(), payload.sourceAudio.end(), 0.0f);
        uint32_t random = 0x1234abcdu;
        for (size_t i = attackStart; i < attackEnd; ++i)
        {
            random = random * 1664525u + 1013904223u;
            float noise = static_cast<float>(random >> 8) / 8388608.0f - 1.0f;
            if (std::abs(noise) < 0.1f)
                noise = noise < 0.0f ? -0.1f : 0.1f;
            payload.sourceAudio[i] = noise * amplitude;
        }
        return payload;
    };

    const std::vector<float> synthesis(sampleCount, 0.2f);
    auto nearSilence = makeAttack(1.0e-7f);
    const auto quietMix = OpenTune::mixHybridRender(synthesis, nearSilence);
    const float quietWeight = (quietMix[attackSample] - synthesis[attackSample])
        / (nearSilence.sourceAudio[attackSample] - synthesis[attackSample]);
    expect(quietWeight < 0.95f,
           "near-silence broadband energy should not force transient input weight");

    auto broadbandAttack = makeAttack(0.25f);
    const auto attackMix = OpenTune::mixHybridRender(synthesis, broadbandAttack);
    expect(attackMix[attackSample] == broadbandAttack.sourceAudio[attackSample],
           "a broadband attack should receive full transient input weight");
}

void testCropAndChunkConsistency()
{
    constexpr size_t totalSamples = 4096;
    auto fullPayload = makeMixPayload(totalSamples, 1);
    fullPayload.globalSampleOffset = 512;
    std::fill(fullPayload.voicedSamples.begin() + 1536,
              fullPayload.voicedSamples.begin() + 3072, 0);
    const std::vector<float> synthesis(totalSamples, 0.25f);
    const auto full = OpenTune::mixHybridRender(synthesis, fullPayload);

    auto chunkPayload = fullPayload;
    chunkPayload.synthesisOffsetSamples = 1200;
    chunkPayload.publishSampleCount = 1000;
    const auto chunk = OpenTune::mixHybridRender(synthesis, chunkPayload);
    expect(chunk.size() == chunkPayload.publishSampleCount,
           "hybrid publication should crop only after global fade decisions");
    bool matches = chunk.size() == chunkPayload.publishSampleCount;
    for (size_t i = 0; matches && i < chunk.size(); ++i)
        matches = std::abs(chunk[i] - full[chunkPayload.synthesisOffsetSamples + i]) < 1.0e-6f;
    expect(matches, "cropped and full-context hybrid mixes should share identical overlap weights");

    auto makeContext = [](int64_t globalOffset, size_t count) {
        auto payload = makeMixPayload(count, 1);
        payload.globalSampleOffset = globalOffset;
        std::vector<float> synthesis(count);
        for (size_t i = 0; i < count; ++i)
        {
            const int64_t globalSample = globalOffset + static_cast<int64_t>(i);
            const float phase = static_cast<float>(globalSample) / 44100.0f;
            payload.sourceAudio[i] = 0.5f + 0.1f * std::sin(2.0f * 3.14159265358979323846f
                                                              * 440.0f * phase);
            synthesis[i] = 0.25f + 0.1f * std::sin(2.0f * 3.14159265358979323846f
                                                   * 330.0f * phase);
            payload.voicedSamples[i] = globalSample >= 2048 && globalSample < 3584 ? 0 : 1;
        }
        return std::make_pair(std::move(payload), std::move(synthesis));
    };
    auto firstContext = makeContext(0, 4096);
    auto secondContext = makeContext(512, 4096);
    firstContext.first.synthesisOffsetSamples = 1024;
    firstContext.first.publishSampleCount = 3072;
    secondContext.first.synthesisOffsetSamples = 512;
    secondContext.first.publishSampleCount = 3072;
    const auto firstOverlap = OpenTune::mixHybridRender(
        firstContext.second, firstContext.first);
    const auto secondOverlap = OpenTune::mixHybridRender(
        secondContext.second, secondContext.first);
    bool chunkWeightsMatch = firstOverlap.size() == secondOverlap.size();
    for (size_t i = 0; chunkWeightsMatch && i < firstOverlap.size(); ++i)
        chunkWeightsMatch = std::abs(firstOverlap[i] - secondOverlap[i]) < 1.0e-5f;
    expect(chunkWeightsMatch,
           "overlapping contexts should choose matching fades on the global sample axis");
}

} // namespace

int main()
{
    testResidualAlignmentAndUv();
    testPeriodicAndAperiodicHighBand();
    testGlobalFrameAlignedContext();
    testUvSideCandidateCentersAndFadeDirections();
    testShortUvTriangularMask();
    testVoicedSynthesisAndResidualMix();
    testShortAdjacentVoicedFade();
    testTransientPreservation();
    testTransientFallbackFadeStaysBesideTransient();
    testTransientFluxThresholdSemantics();
    testCropAndChunkConsistency();
    return failures == 0 ? 0 : 1;
}
