
#include "CaptureRingBuffer.h"

#include <cstring>

namespace OpenTune::Capture {

CaptureRingBuffer::CaptureRingBuffer() = default;

void CaptureRingBuffer::reserve(int numChannels, int capacitySamples)
{
    numChannels_ = juce::jmax(1, numChannels);
    capacitySamples_ = juce::jmax(1, capacitySamples);
    buffer_.setSize(numChannels_, capacitySamples_, /*keepExistingContent*/ false,
                    /*clearExtraSpace*/ true, /*avoidReallocating*/ false);
}

void CaptureRingBuffer::release()
{
    buffer_.setSize(0, 0);
    numChannels_ = 0;
    capacitySamples_ = 0;
}

void CaptureRingBuffer::writeAt(int windowOffset, const float* const* srcChannels,
                                int numSourceChannels, int numSamples) noexcept
{
    if (numSamples <= 0 || numChannels_ == 0 || capacitySamples_ == 0)
        return;
    if (windowOffset < 0 || windowOffset >= capacitySamples_ || numSamples > capacitySamples_)
        return;

    const int channelsToCopy = juce::jmin(numSourceChannels, numChannels_);
    const int firstCount = juce::jmin(numSamples, capacitySamples_ - windowOffset);

    for (int ch = 0; ch < numChannels_; ++ch) {
        const bool hasSource = ch < channelsToCopy && srcChannels[ch] != nullptr;
        float* tail = buffer_.getWritePointer(ch, windowOffset);
        if (hasSource)
            std::memcpy(tail, srcChannels[ch], static_cast<size_t>(firstCount) * sizeof(float));
        else
            std::memset(tail, 0, static_cast<size_t>(firstCount) * sizeof(float));

        const int secondCount = numSamples - firstCount;
        if (secondCount <= 0)
            continue;

        float* head = buffer_.getWritePointer(ch, 0);
        if (hasSource)
            std::memcpy(head, srcChannels[ch] + firstCount,
                        static_cast<size_t>(secondCount) * sizeof(float));
        else
            std::memset(head, 0, static_cast<size_t>(secondCount) * sizeof(float));
    }
}

void CaptureRingBuffer::readRange(int windowOffset, juce::AudioBuffer<float>& dest,
                                  int destOffset, int numSamples) const noexcept
{
    if (numSamples <= 0 || numChannels_ == 0 || capacitySamples_ == 0)
        return;
    if (windowOffset < 0 || windowOffset >= capacitySamples_)
        return;

    const int writable = juce::jmin(dest.getNumSamples() - destOffset, numSamples);
    if (writable <= 0)
        return;

    const int firstCount = juce::jmin(writable, capacitySamples_ - windowOffset);
    for (int ch = 0; ch < numChannels_; ++ch) {
        const float* tail = buffer_.getReadPointer(ch, windowOffset);
        dest.copyFrom(ch, destOffset, tail, firstCount);

        const int secondCount = writable - firstCount;
        if (secondCount > 0) {
            const float* head = buffer_.getReadPointer(ch, 0);
            dest.copyFrom(ch, destOffset + firstCount, head, secondCount);
        }
    }
}

}  // namespace OpenTune::Capture
