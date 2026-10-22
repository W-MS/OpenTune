#pragma once

#include <cmath>
#include <cstdint>

namespace OpenTune {

// Peaks are buckets of raw audio samples. Source time is seconds on that
// same sample clock. A fixed 44100 Hz clock walks away from notes whenever
// the file was recorded at another rate: both start together and the gap
// grows in proportion to time.
inline double waveformSecondsPerPeak(int samplesPerPeak, double sampleRate) noexcept
{
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    if (samplesPerPeak <= 0 || !std::isfinite(rate))
        return 0.0;
    return static_cast<double>(samplesPerPeak) / rate;
}

inline int64_t waveformPeakIndexAtSourceTime(double sourceSeconds,
                                             int samplesPerPeak,
                                             double sampleRate) noexcept
{
    if (!(sourceSeconds >= 0.0) || !std::isfinite(sourceSeconds))
        return -1;
    const double secondsPerPeak = waveformSecondsPerPeak(samplesPerPeak, sampleRate);
    if (!(secondsPerPeak > 0.0))
        return -1;
    return static_cast<int64_t>(std::floor(sourceSeconds / secondsPerPeak));
}

} // namespace OpenTune
