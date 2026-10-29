#pragma once


#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace OpenTune::Capture {

/**
 * Host-addressed PCM window for one take.
 *
 * Writes come from the audio thread (CaptureSession::processBlock):
 *   - writeAt() is noexcept, never allocates, never locks.
 *   - The caller supplies the window offset; an offset near the end is split
 *     into two contiguous copies (tail, then head).
 *
 * Reads come from the message thread (CaptureSession::finalizePendingCapture)
 * at the same window addresses, via readRange(), which copies a host-addressed
 * range into an output buffer. Both sides only touch storage pre-allocated by
 * reserve().
 *
 * The window stores PCM only; validity is expressed by the take's span list.
 */
class CaptureRingBuffer
{
public:
    CaptureRingBuffer();

    /** Allocate the underlying buffer. Message thread only. */
    void reserve(int numChannels, int capacitySamples);

    /** Release the underlying buffer. Message thread only. */
    void release();

    int getCapacitySamples() const noexcept { return capacitySamples_; }
    int getNumChannels() const noexcept { return numChannels_; }

    /** Audio-thread-safe window write at 'windowOffset', wrapping at the end. */
    void writeAt(int windowOffset, const float* const* srcChannels, int numSourceChannels,
                 int numSamples) noexcept;

    /** Message-thread read of 'numSamples' starting at 'windowOffset' into
     *  dest[destOffset...]. */
    void readRange(int windowOffset, juce::AudioBuffer<float>& dest, int destOffset,
                   int numSamples) const noexcept;

private:
    int numChannels_ = 0;
    int capacitySamples_ = 0;
    juce::AudioBuffer<float> buffer_;
};

}  // namespace OpenTune::Capture
