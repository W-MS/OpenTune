#include "HybridAperiodicResidual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace OpenTune {
namespace {

constexpr int kSampleRate = 44100;
constexpr int kFftSize = 2048;
constexpr int kHopSize = 512;
constexpr int kLeftPad = (kFftSize - kHopSize) / 2;
constexpr int kCrossfadeSamples = 220;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kBoundarySearchRadius = 512;
constexpr int kBoundaryCandidateHop = 128;
constexpr int kTransientFftSize = 512;
constexpr int kTransientHopSize = 128;
constexpr int kLowFrequencyWindow = 441;
constexpr double kTransientFluxThreshold = 0.30;

struct SampleRange
{
    int64_t start{0};
    int64_t end{0};
};

struct BiquadCoefficients
{
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
};

struct BoundaryCandidate
{
    int64_t center;
    double lowFrequencyRmsSum;
    double spectralCentroid;
};

struct BoundaryPosition
{
    int64_t center;
    const SampleRange* transient{nullptr};
};

void fft(std::vector<std::complex<float>>& values, bool inverse)
{
    const size_t size = values.size();
    for (size_t i = 1, j = 0; i < size; ++i)
    {
        size_t bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(values[i], values[j]);
    }

    for (size_t length = 2; length <= size; length <<= 1)
    {
        const float angle = (inverse ? 2.0f : -2.0f) * kPi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t start = 0; start < size; start += length)
        {
            std::complex<float> factor(1.0f, 0.0f);
            for (size_t i = 0; i < length / 2; ++i)
            {
                const auto even = values[start + i];
                const auto odd = values[start + i + length / 2] * factor;
                values[start + i] = even + odd;
                values[start + i + length / 2] = even - odd;
                factor *= step;
            }
        }
    }

    if (inverse)
        for (auto& value : values)
            value /= static_cast<float>(size);
}

int reflectIndex(int index, int size)
{
    while (index < 0 || index >= size)
    {
        if (index < 0)
            index = -index;
        if (index >= size)
            index = 2 * size - index - 2;
    }
    return index;
}

int64_t ceilToGrid(int64_t sample, int64_t grid)
{
    const int64_t remainder = ((sample % grid) + grid) % grid;
    return remainder == 0 ? sample : sample + grid - remainder;
}

float sampleAtGlobal(const std::vector<float>& audio,
                     int64_t globalOffset,
                     int64_t globalSample)
{
    const int64_t localSample = globalSample - globalOffset;
    return localSample >= 0 && localSample < static_cast<int64_t>(audio.size())
        ? audio[static_cast<size_t>(localSample)] : 0.0f;
}

std::vector<SampleRange> detectTransientRanges(const std::vector<float>& audio,
                                                int64_t globalOffset)
{
    std::vector<SampleRange> ranges;
    if (audio.empty())
        return ranges;

    constexpr int kFirstBin = 12;
    constexpr int kLastBin = 185;
    std::vector<float> window(kTransientFftSize);
    for (int i = 0; i < kTransientFftSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            2.0f * kPi * i / kTransientFftSize));

    const int64_t globalEnd = globalOffset + static_cast<int64_t>(audio.size());
    int64_t frameStart = ceilToGrid(globalOffset - kTransientFftSize,
                                    kTransientHopSize);
    bool hasPrevious = false;
    double previousEnergy = 0.0;
    std::vector<std::complex<float>> spectrum(kTransientFftSize);
    for (; frameStart < globalEnd; frameStart += kTransientHopSize)
    {
        for (int i = 0; i < kTransientFftSize; ++i)
        {
            const float sample = sampleAtGlobal(audio, globalOffset, frameStart + i);
            spectrum[static_cast<size_t>(i)] = {
                sample * window[static_cast<size_t>(i)], 0.0f};
        }
        fft(spectrum, false);

        double currentLogMagnitudeSum = 0.0;
        for (int bin = kFirstBin; bin <= kLastBin; ++bin)
        {
            const float value = std::abs(spectrum[static_cast<size_t>(bin)]);
            currentLogMagnitudeSum += std::log(std::max(value, 1.0e-5f));
        }

        const double energy = std::exp(currentLogMagnitudeSum
            / (kLastBin - kFirstBin + 1));
        const double flux = std::max(energy - previousEnergy, 0.0)
            / (energy + previousEnergy + 1.0e-6);
        if (hasPrevious && flux >= kTransientFluxThreshold)
            ranges.push_back({frameStart, frameStart + kTransientFftSize});
        previousEnergy = energy;
        hasPrevious = true;
    }

    std::vector<SampleRange> merged;
    for (const auto& range : ranges)
    {
        if (!merged.empty() && range.start <= merged.back().end)
            merged.back().end = std::max(merged.back().end, range.end);
        else
            merged.push_back(range);
    }
    return merged;
}

void filterPass(std::vector<float>& audio, const BiquadCoefficients& coefficients)
{
    if (audio.empty())
        return;

    double x1 = audio.front();
    double x2 = x1;
    double y1 = x1;
    double y2 = x1;
    for (auto& sample : audio)
    {
        const double input = sample;
        const double output = coefficients.b0 * input
            + coefficients.b1 * x1 + coefficients.b2 * x2
            - coefficients.a1 * y1 - coefficients.a2 * y2;
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
        sample = static_cast<float>(output);
    }
}

void lowpassZeroPhase(std::vector<float>& audio)
{
    if (audio.empty())
        return;

    constexpr std::array<double, 2> qualityFactors{
        0.541196100146197, 1.306562964876377};
    const double omega = 2.0 * static_cast<double>(kPi) * 1500.0 / kSampleRate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    std::array<BiquadCoefficients, 2> sections{};
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const double alpha = sine / (2.0 * qualityFactors[i]);
        const double scale = 1.0 / (1.0 + alpha);
        sections[i] = {
            (1.0 - cosine) * 0.5 * scale,
            (1.0 - cosine) * scale,
            (1.0 - cosine) * 0.5 * scale,
            -2.0 * cosine * scale,
            (1.0 - alpha) * scale};
    }

    for (const auto& section : sections)
        filterPass(audio, section);
    std::reverse(audio.begin(), audio.end());
    for (const auto& section : sections)
        filterPass(audio, section);
    std::reverse(audio.begin(), audio.end());
}

double lowFrequencyRms(const std::vector<float>& filtered,
                       int64_t center,
                       int64_t globalOffset)
{
    const int64_t first = center - kLowFrequencyWindow / 2;
    double energy = 0.0;
    for (int i = 0; i < kLowFrequencyWindow; ++i)
    {
        const int64_t local = first + i - globalOffset;
        if (local >= 0 && local < static_cast<int64_t>(filtered.size()))
        {
            const double sample = filtered[static_cast<size_t>(local)];
            energy += sample * sample;
        }
    }
    return std::sqrt(energy / kLowFrequencyWindow);
}

double spectralCentroid(const std::vector<float>& audio,
                        int64_t center,
                        int64_t globalOffset)
{
    constexpr int kBinCount = kTransientFftSize / 2 + 1;
    std::vector<std::complex<float>> spectrum(kTransientFftSize);
    for (int i = 0; i < kTransientFftSize; ++i)
    {
        const int64_t local = center - kTransientFftSize / 2 + i - globalOffset;
        const float sample = local >= 0 && local < static_cast<int64_t>(audio.size())
            ? audio[static_cast<size_t>(local)] : 0.0f;
        const float window = 0.5f * (1.0f - std::cos(
            2.0f * kPi * i / kTransientFftSize));
        spectrum[static_cast<size_t>(i)] = {sample * window, 0.0f};
    }
    fft(spectrum, false);

    double magnitudeSum = 0.0;
    double weightedBinSum = 0.0;
    for (int bin = 0; bin < kBinCount; ++bin)
    {
        const double magnitude = std::abs(spectrum[static_cast<size_t>(bin)]);
        magnitudeSum += magnitude;
        weightedBinSum += bin * magnitude;
    }
    return magnitudeSum > 0.0 ? weightedBinSum / magnitudeSum : 0.0;
}

bool overlapsTransient(const std::vector<SampleRange>& transients,
                       int64_t start,
                       int64_t end)
{
    return std::any_of(transients.begin(), transients.end(),
        [start, end](const SampleRange& range) {
            return range.start < end && range.end > start;
        });
}

std::vector<BoundaryPosition> rankBoundaryCandidates(
    const std::vector<float>& sourceAudio,
    const std::vector<float>& filteredSource,
    const std::vector<float>& filteredSynthesis,
    int64_t globalOffset,
    int64_t uvStart,
    int64_t uvEnd,
    int64_t transition,
    bool enteringUv,
    const std::vector<SampleRange>& transients)
{
    const int64_t areaStart = std::max(uvStart, transition - kBoundarySearchRadius);
    const int64_t areaEnd = std::min(uvEnd - 1, transition + kBoundarySearchRadius);
    std::vector<BoundaryCandidate> candidates;
    if (areaStart <= areaEnd)
    {
        for (int64_t center = ceilToGrid(areaStart, kBoundaryCandidateHop);
             center <= areaEnd; center += kBoundaryCandidateHop)
        {
            const int64_t windowStart = center - kTransientFftSize / 2;
            const int64_t windowEnd = windowStart + kTransientFftSize;
            if (overlapsTransient(transients, windowStart, windowEnd))
                continue;

            candidates.push_back({center,
                lowFrequencyRms(filteredSource, center, globalOffset)
                    + lowFrequencyRms(filteredSynthesis, center, globalOffset),
                spectralCentroid(sourceAudio, center, globalOffset)});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const BoundaryCandidate& left, const BoundaryCandidate& right) {
            if (left.lowFrequencyRmsSum != right.lowFrequencyRmsSum)
                return left.lowFrequencyRmsSum < right.lowFrequencyRmsSum;
            if (left.spectralCentroid != right.spectralCentroid)
                return left.spectralCentroid > right.spectralCentroid;
            return left.center < right.center;
        });

    std::vector<BoundaryPosition> ranked;
    ranked.reserve(candidates.size());
    for (const auto& candidate : candidates)
        ranked.push_back({candidate.center});
    if (!ranked.empty())
        return ranked;

    const SampleRange* nearestTransient = nullptr;
    int64_t fallbackCenter = enteringUv ? areaStart : areaEnd;
    int64_t nearestDistance = std::numeric_limits<int64_t>::max();
    for (const auto& transient : transients)
    {
        if (transient.end <= areaStart || transient.start > areaEnd)
            continue;
        const int64_t edge = enteringUv
            ? std::max(areaStart, transient.start)
            : std::min(areaEnd, transient.end - 1);
        const int64_t distance = std::abs(edge - transition);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTransient = &transient;
            fallbackCenter = edge;
        }
    }
    if (nearestTransient != nullptr)
    {
        // Keep the fallback candidate on the UV side; its fade is placed beside
        // the transient on the edge facing the transition.
        return {{fallbackCenter, nearestTransient}};
    }

    const int64_t firstGridCenter = ceilToGrid(areaStart, kBoundaryCandidateHop);
    if (firstGridCenter <= areaEnd)
        return {{enteringUv ? firstGridCenter
                             : firstGridCenter + ((areaEnd - firstGridCenter)
                                 / kBoundaryCandidateHop) * kBoundaryCandidateHop}};
    return {{enteringUv ? uvStart : uvEnd - 1}};
}

void setWeightRange(std::vector<float>& weights,
                    int64_t globalOffset,
                    int64_t start,
                    int64_t end,
                    float value)
{
    const int64_t clippedStart = std::max(start, globalOffset);
    const int64_t clippedEnd = std::min(end,
        globalOffset + static_cast<int64_t>(weights.size()));
    for (int64_t sample = clippedStart; sample < clippedEnd; ++sample)
        weights[static_cast<size_t>(sample - globalOffset)] = value;
}

void applyCenteredFade(std::vector<float>& weights,
                       int64_t globalOffset,
                       int64_t center,
                       bool enteringUv,
                       int fadeLength)
{
    const int64_t fadeStart = center - fadeLength / 2;
    for (int i = 0; i < fadeLength; ++i)
    {
        const int64_t sample = fadeStart + i;
        const int64_t local = sample - globalOffset;
        if (local < 0 || local >= static_cast<int64_t>(weights.size()))
            continue;
        const float ramp = static_cast<float>(i) / fadeLength;
        weights[static_cast<size_t>(local)] = enteringUv ? ramp : 1.0f - ramp;
    }
}

struct AdjacentFade
{
    int64_t start;
    int length;
    int64_t center;
    bool rising;
};

std::vector<AdjacentFade> adjacentTransientFades(
    int64_t globalOffset,
    int64_t globalEnd,
    int64_t uvStart,
    int64_t uvEnd,
    int fadeLength,
    const SampleRange& transient,
    const std::vector<SampleRange>& transients)
{
    const int64_t transientStart = std::max(transient.start, globalOffset);
    const int64_t transientEnd = std::min(transient.end, globalEnd);
    int64_t beforeLimit = globalOffset;
    int64_t afterLimit = globalEnd;
    for (const auto& other : transients)
    {
        if (other.end <= transient.start)
            beforeLimit = std::max(beforeLimit, other.end);
        if (other.start >= transient.end)
            afterLimit = std::min(afterLimit, other.start);
    }

    const int beforeLength = static_cast<int>(std::min<int64_t>(
        fadeLength, std::max<int64_t>(0, transientStart - beforeLimit)));
    const int afterLength = static_cast<int>(std::min<int64_t>(
        fadeLength, std::max<int64_t>(0, afterLimit - transientEnd)));
    std::vector<AdjacentFade> fades;
    if (beforeLength > 0)
    {
        const int64_t start = transientStart - beforeLength;
        fades.push_back({start, beforeLength, start + beforeLength / 2, true});
    }
    if (afterLength > 0)
    {
        const int64_t start = transientEnd;
        fades.push_back({start, afterLength, start + afterLength / 2, false});
    }

    const auto uvSide = [uvStart, uvEnd](const AdjacentFade& fade) {
        return fade.center >= uvStart && fade.center < uvEnd;
    };
    const bool hasUvSideFade = std::any_of(fades.begin(), fades.end(), uvSide);
    fades.erase(std::remove_if(fades.begin(), fades.end(),
        [&uvSide, hasUvSideFade](const AdjacentFade& fade) {
            return hasUvSideFade && !uvSide(fade);
        }), fades.end());
    return fades;
}

void applyTransientFallbackFade(std::vector<float>& weights,
                                int64_t globalOffset,
                                int64_t transition,
                                int64_t uvStart,
                                int64_t uvEnd,
                                bool enteringUv,
                                const SampleRange& transient,
                                const std::vector<SampleRange>& transients)
{
    const int64_t globalEnd = globalOffset + static_cast<int64_t>(weights.size());
    auto fades = adjacentTransientFades(globalOffset, globalEnd, uvStart, uvEnd,
                                        kCrossfadeSamples, transient, transients);
    if (fades.empty())
        return;

    auto selected = fades.begin();
    if (enteringUv)
    {
        selected = std::min_element(fades.begin(), fades.end(),
            [transition](const AdjacentFade& left, const AdjacentFade& right) {
                return std::abs(left.center - transition)
                    < std::abs(right.center - transition);
            });
    }
    else
    {
        const auto afterTransient = std::find_if(fades.begin(), fades.end(),
            [](const AdjacentFade& fade) { return !fade.rising; });
        if (afterTransient != fades.end())
            selected = afterTransient;
        else
            selected = fades.begin();
    }

    const int64_t fadeEnd = selected->start + selected->length;
    if (enteringUv && selected->rising)
        setWeightRange(weights, globalOffset, transition, selected->start, 0.0f);
    else if (enteringUv)
        setWeightRange(weights, globalOffset, fadeEnd, uvEnd, 0.0f);
    else if (!selected->rising)
        setWeightRange(weights, globalOffset, fadeEnd, transition, 0.0f);

    for (int i = 0; i < selected->length; ++i)
    {
        const int64_t sample = selected->start + i;
        const int64_t local = sample - globalOffset;
        const float ramp = static_cast<float>(i) / selected->length;
        weights[static_cast<size_t>(local)] = selected->rising ? ramp : 1.0f - ramp;
    }
}

void smoothTransientEdges(std::vector<float>& weights,
                          int64_t globalOffset,
                          const SampleRange& transient)
{
    const int64_t globalEnd = globalOffset + static_cast<int64_t>(weights.size());
    const int64_t start = std::max(transient.start, globalOffset);
    const int64_t end = std::min(transient.end, globalEnd);
    if (start >= end)
        return;
    const int64_t beforeStart = std::max(globalOffset, start - kCrossfadeSamples);
    const int64_t beforeLength = start - beforeStart;
    if (beforeLength > 0)
    {
        const float initial = weights[static_cast<size_t>(beforeStart - globalOffset)];
        for (int64_t i = 0; i < beforeLength; ++i)
        {
            const float amount = static_cast<float>(i + 1) / static_cast<float>(beforeLength);
            weights[static_cast<size_t>(beforeStart + i - globalOffset)]
                = initial + (1.0f - initial) * amount;
        }
    }

    const int64_t afterEnd = std::min(globalEnd, end + kCrossfadeSamples);
    const int64_t afterLength = afterEnd - end;
    if (afterLength > 0)
    {
        const float target = afterEnd < globalEnd
            ? weights[static_cast<size_t>(afterEnd - globalOffset)]
            : weights.back();
        for (int64_t i = 0; i < afterLength; ++i)
        {
            const float amount = static_cast<float>(i + 1) / static_cast<float>(afterLength);
            weights[static_cast<size_t>(end + i - globalOffset)]
                = 1.0f + (target - 1.0f) * amount;
        }
    }
}

void applyUvBoundary(std::vector<float>& weights,
                     int64_t globalOffset,
                     int64_t transition,
                     int64_t uvStart,
                     int64_t uvEnd,
                     const BoundaryPosition& position,
                     bool enteringUv,
                     int fadeLength,
                     const std::vector<SampleRange>& transients)
{
    if (position.transient != nullptr)
    {
        applyTransientFallbackFade(weights, globalOffset, transition,
                                   uvStart, uvEnd, enteringUv,
                                   *position.transient, transients);
        return;
    }

    const int64_t center = position.center;
    const int64_t fadeStart = center - fadeLength / 2;
    const int64_t fadeEnd = fadeStart + fadeLength;
    if (enteringUv && fadeStart > transition)
        setWeightRange(weights, globalOffset, transition, fadeStart, 0.0f);
    if (!enteringUv && fadeEnd < transition)
        setWeightRange(weights, globalOffset, fadeEnd, transition, 0.0f);
    applyCenteredFade(weights, globalOffset, center, enteringUv, fadeLength);
}

void applyShortUvTriangle(std::vector<float>& weights,
                          int64_t globalOffset,
                          int64_t uvStart,
                          int64_t uvEnd)
{
    const double center = (static_cast<double>(uvStart)
        + static_cast<double>(uvEnd - 1)) * 0.5;
    const double leftEdge = static_cast<double>(uvStart - 1);
    const double rightEdge = static_cast<double>(uvEnd);
    for (int64_t sample = uvStart; sample < uvEnd; ++sample)
    {
        const double position = static_cast<double>(sample);
        const double weight = position <= center
            ? (position - leftEdge) / (center - leftEdge)
            : (rightEdge - position) / (rightEdge - center);
        const int64_t local = sample - globalOffset;
        if (local >= 0 && local < static_cast<int64_t>(weights.size()))
            weights[static_cast<size_t>(local)] = static_cast<float>(weight);
    }
}

} // namespace

size_t HybridRenderPayload::retainedBytes() const noexcept
{
    return sourceAudio.capacity() * sizeof(float)
        + residual.capacity() * sizeof(float)
        + voicedSamples.capacity() * sizeof(uint8_t);
}

HybridAperiodicResult extractAperiodicHigh(const float* audio,
                                           size_t audioSamples,
                                           const std::vector<float>& f0,
                                           const std::vector<uint8_t>& voiced,
                                           size_t firstGlobalFrame,
                                           size_t totalFrameCount)
{
    if (f0.size() != voiced.size())
        throw std::invalid_argument("Hybrid F0 and voiced frame counts differ");

    const size_t frameCount = f0.size();
    const size_t outputSamples = frameCount * kHopSize;
    if (totalFrameCount == 0)
        totalFrameCount = frameCount;
    const size_t targetSamples = std::max(totalFrameCount * kHopSize,
                                          static_cast<size_t>(kFftSize));
    const auto alignedSample = [audio, audioSamples, targetSamples](int64_t index) {
        const auto reflected = static_cast<size_t>(reflectIndex(
            static_cast<int>(index), static_cast<int>(targetSamples)));
        return reflected < audioSamples ? audio[reflected] : 0.0f;
    };

    HybridAperiodicResult result;
    result.alignedAudio.resize(outputSamples);
    for (size_t i = 0; i < outputSamples; ++i)
        result.alignedAudio[i] = alignedSample(static_cast<int64_t>(
            firstGlobalFrame * kHopSize + i));
    result.residual.assign(outputSamples, 0.0f);
    if (frameCount == 0)
        return result;

    constexpr int kBinCount = kFftSize / 2 + 1;
    std::vector<float> window(kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * kPi * i / kFftSize));

    std::vector<float> highBand(kBinCount, 0.0f);
    for (int bin = 0; bin < kBinCount; ++bin)
    {
        const float frequency = static_cast<float>(bin) * kSampleRate / kFftSize;
        if (frequency >= 12000.0f)
            highBand[static_cast<size_t>(bin)] = 1.0f;
        else if (frequency >= 11000.0f)
            highBand[static_cast<size_t>(bin)] = 0.5f - 0.5f * std::cos(
                kPi * (frequency - 11000.0f) / 1000.0f);
    }

    std::vector<float> harmonicMask(frameCount * kBinCount, 0.0f);
    float minimumVoicedF0 = 0.0f;
    for (size_t frame = 0; frame < frameCount; ++frame)
        if (voiced[frame] != 0 && f0[frame] > 0.0f
            && (minimumVoicedF0 == 0.0f || f0[frame] < minimumVoicedF0))
            minimumVoicedF0 = f0[frame];

    if (minimumVoicedF0 > 0.0f)
    {
        const int maxHarmonic = static_cast<int>(std::floor(
            (kSampleRate / 2.0f) / minimumVoicedF0));
        const float binHz = static_cast<float>(kSampleRate) / kFftSize;
        for (size_t frame = 0; frame < frameCount; ++frame)
        {
            if (voiced[frame] == 0 || f0[frame] <= 0.0f)
                continue;
            for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic)
            {
                const float position = f0[frame] * static_cast<float>(harmonic) / binHz;
                if (position > kFftSize / 2)
                    continue;
                const float floorPosition = std::floor(position);
                const float fractionalPosition = position - floorPosition;
                const int lowerBin = static_cast<int>(floorPosition);
                const int nearest = fractionalPosition < 0.5f ? lowerBin
                    : (fractionalPosition > 0.5f || (lowerBin & 1) != 0
                        ? lowerBin + 1 : lowerBin);
                const int firstBin = std::max(0, nearest - 6);
                const int lastBin = std::min(kFftSize / 2, nearest + 6);
                for (int bin = firstBin; bin <= lastBin; ++bin)
                {
                    const float distance = position - static_cast<float>(bin);
                    const float weight = std::exp(-0.5f * distance * distance);
                    auto& mask = harmonicMask[frame * kBinCount + static_cast<size_t>(bin)];
                    mask = std::max(mask, weight);
                }
            }
        }
    }

    const size_t overlapLength = (frameCount - 1) * kHopSize + kFftSize;
    std::vector<float> overlapAdd(overlapLength, 0.0f);
    std::vector<float> normalization(overlapLength, 0.0f);
    std::vector<std::complex<float>> spectrum(kFftSize);
    for (size_t frame = 0; frame < frameCount; ++frame)
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const int64_t sourceIndex = static_cast<int64_t>(
                (firstGlobalFrame + frame) * kHopSize) + i - kLeftPad;
            const float sample = alignedSample(sourceIndex);
            spectrum[static_cast<size_t>(i)] = {sample * window[static_cast<size_t>(i)], 0.0f};
        }
        fft(spectrum, false);

        if (voiced[frame] == 0)
            std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
        else
        {
            for (int bin = 0; bin < kBinCount; ++bin)
            {
                const float mask = harmonicMask[frame * kBinCount + static_cast<size_t>(bin)];
                const float weight = highBand[static_cast<size_t>(bin)] * (1.0f - mask);
                spectrum[static_cast<size_t>(bin)] *= weight;
                if (bin > 0 && bin < kFftSize / 2)
                    spectrum[static_cast<size_t>(kFftSize - bin)] = std::conj(spectrum[static_cast<size_t>(bin)]);
            }
        }

        fft(spectrum, true);
        const size_t frameStart = frame * kHopSize;
        for (int i = 0; i < kFftSize; ++i)
        {
            const size_t index = frameStart + static_cast<size_t>(i);
            const float win = window[static_cast<size_t>(i)];
            overlapAdd[index] += spectrum[static_cast<size_t>(i)].real() * win;
            normalization[index] += win * win;
        }
    }

    for (size_t i = 0; i < outputSamples; ++i)
    {
        const size_t index = i + kLeftPad;
        if (voiced[i / kHopSize] != 0)
            result.residual[i] = overlapAdd[index] / std::max(normalization[index], 1.0e-8f);
    }
    return result;
}

std::vector<float> mixHybridRender(const std::vector<float>& synthesis,
                                   const HybridRenderPayload& payload)
{
    const size_t offset = payload.synthesisOffsetSamples;
    const size_t count = payload.publishSampleCount;
    if (synthesis.size() != payload.sourceAudio.size()
        || payload.residual.size() != payload.sourceAudio.size()
        || payload.voicedSamples.size() != payload.sourceAudio.size()
        || offset + count > synthesis.size())
        return {};

    const int64_t globalOffset = payload.globalSampleOffset;
    const int64_t sampleCount = static_cast<int64_t>(payload.sourceAudio.size());
    const int64_t globalEnd = globalOffset + sampleCount;
    std::vector<float> inputWeight(payload.sourceAudio.size(), 0.0f);
    for (size_t i = 0; i < payload.voicedSamples.size(); ++i)
        inputWeight[i] = payload.voicedSamples[i] != 0 ? 0.0f : 1.0f;

    auto filteredSource = payload.sourceAudio;
    auto filteredSynthesis = synthesis;
    lowpassZeroPhase(filteredSource);
    lowpassZeroPhase(filteredSynthesis);
    const auto transients = detectTransientRanges(payload.sourceAudio, globalOffset);

    for (size_t position = 0; position < payload.voicedSamples.size();)
    {
        if (payload.voicedSamples[position] != 0)
        {
            ++position;
            continue;
        }

        const size_t runStart = position;
        while (position < payload.voicedSamples.size()
               && payload.voicedSamples[position] == 0)
            ++position;
        const size_t runEnd = position;
        const int64_t uvStart = globalOffset + static_cast<int64_t>(runStart);
        const int64_t uvEnd = globalOffset + static_cast<int64_t>(runEnd);
        const int64_t runLength = static_cast<int64_t>(runEnd - runStart);
        const bool hasEnteringBoundary = runStart > 0;
        const bool hasExitingBoundary = runEnd < payload.voicedSamples.size();

        if (runLength < kLowFrequencyWindow
            && hasEnteringBoundary && hasExitingBoundary)
        {
            applyShortUvTriangle(inputWeight, globalOffset, uvStart, uvEnd);
            continue;
        }

        std::vector<BoundaryPosition> enteringCandidates;
        std::vector<BoundaryPosition> exitingCandidates;
        if (hasEnteringBoundary)
            enteringCandidates = rankBoundaryCandidates(
                payload.sourceAudio, filteredSource, filteredSynthesis,
                globalOffset, uvStart, uvEnd, uvStart, true, transients);
        if (hasExitingBoundary)
            exitingCandidates = rankBoundaryCandidates(
                payload.sourceAudio, filteredSource, filteredSynthesis,
                globalOffset, uvStart, uvEnd, uvEnd, false, transients);

        BoundaryPosition enteringPosition{uvStart};
        BoundaryPosition exitingPosition{uvEnd - 1};
        if (hasEnteringBoundary && hasExitingBoundary)
        {
            bool foundSeparateFades = false;
            for (const auto left : enteringCandidates)
            {
                const auto right = std::find_if(exitingCandidates.begin(), exitingCandidates.end(),
                    [left](const BoundaryPosition& candidate) {
                        return candidate.center - left.center >= kCrossfadeSamples;
                    });
                if (right != exitingCandidates.end())
                {
                    enteringPosition = left;
                    exitingPosition = *right;
                    foundSeparateFades = true;
                    break;
                }
            }
            if (!foundSeparateFades)
            {
                enteringPosition = enteringCandidates.front().transient != nullptr
                    ? enteringCandidates.front() : BoundaryPosition{uvStart};
                exitingPosition = exitingCandidates.front().transient != nullptr
                    ? exitingCandidates.front() : BoundaryPosition{uvEnd - 1};
            }
        }
        else if (hasEnteringBoundary)
            enteringPosition = enteringCandidates.front();
        else if (hasExitingBoundary)
            exitingPosition = exitingCandidates.front();

        if (hasEnteringBoundary)
        {
            size_t voicedStart = runStart;
            while (voicedStart > 0
                   && payload.voicedSamples[voicedStart - 1] != 0)
                --voicedStart;
            const int fadeLength = std::min(kCrossfadeSamples,
                static_cast<int>(runStart - voicedStart));
            applyUvBoundary(inputWeight, globalOffset, uvStart, uvStart, uvEnd,
                            enteringPosition, true, fadeLength, transients);
        }

        if (hasExitingBoundary)
        {
            size_t voicedEnd = runEnd;
            while (voicedEnd < payload.voicedSamples.size()
                   && payload.voicedSamples[voicedEnd] != 0)
                ++voicedEnd;
            const int fadeLength = std::min(kCrossfadeSamples,
                static_cast<int>(voicedEnd - runEnd));
            applyUvBoundary(inputWeight, globalOffset, uvEnd, uvStart, uvEnd,
                            exitingPosition, false, fadeLength, transients);
        }

    }

    for (const auto& transient : transients)
    {
        const int64_t start = std::max(transient.start, globalOffset);
        const int64_t end = std::min(transient.end, globalEnd);
        bool overlapsUv = false;
        for (int64_t sample = start; sample < end; ++sample)
        {
            const size_t local = static_cast<size_t>(sample - globalOffset);
            if (payload.voicedSamples[local] == 0)
                overlapsUv = true;
        }
        if (overlapsUv)
        {
            for (int64_t sample = start; sample < end; ++sample)
                inputWeight[static_cast<size_t>(sample - globalOffset)] = 1.0f;
            smoothTransientEdges(inputWeight, globalOffset, transient);
        }
    }

    std::vector<float> output(count);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t sample = offset + i;
        const float voiced = synthesis[sample] + payload.residual[sample];
        const float input = inputWeight[sample];
        output[i] = (1.0f - input) * voiced + input * payload.sourceAudio[sample];
    }
    return output;
}

} // namespace OpenTune
