#include "EqFilterEditOps.h"

#include <algorithm>

namespace OpenTune {

EqFilter makeEqFilter(double freqHz, double gainDb)
{
    EqFilter f;
    f.paletteSlot = -1;
    f.q = 1.0f;

    if (freqHz < 200.0)
        f.type = EqFilterType::LowShelf;
    else if (freqHz > 10000.0)
        f.type = EqFilterType::HighShelf;
    else
        f.type = EqFilterType::Peak;

    setEqFilterFrequencyAndGain(f, freqHz, gainDb);
    return f;
}

void setEqFilterType(EqFilter& filter, EqFilterType newType)
{
    filter.type = newType;
    if (newType == EqFilterType::Peak)
        filter.frequencyHz = static_cast<float>(std::clamp<double>(filter.frequencyHz, 500.0, 12000.0));
    else
        filter.frequencyHz = static_cast<float>(std::clamp<double>(
            filter.frequencyHz, EqSettings::kMinFrequencyHz, EqSettings::kMaxFrequencyHz));
    if (newType == EqFilterType::LowCut || newType == EqFilterType::HighCut)
        filter.gainDb = 0.0f;
}

void setEqFilterFrequencyAndGain(EqFilter& filter, double freqHz, double gainDb)
{
    if (filter.type == EqFilterType::Peak)
        filter.frequencyHz = static_cast<float>(std::clamp(freqHz, 500.0, 12000.0));
    else
        filter.frequencyHz = static_cast<float>(std::clamp(
            freqHz, static_cast<double>(EqSettings::kMinFrequencyHz),
            static_cast<double>(EqSettings::kMaxFrequencyHz)));

    if (filter.type == EqFilterType::LowCut || filter.type == EqFilterType::HighCut)
        filter.gainDb = 0.0f;
    else
        filter.gainDb = static_cast<float>(std::clamp(
            gainDb, static_cast<double>(EqSettings::kMinGainDb),
            static_cast<double>(EqSettings::kMaxGainDb)));
}

void adjustEqFilterQ(EqFilter& filter, double delta)
{
    filter.q = static_cast<float>(std::clamp(
        static_cast<double>(filter.q) + delta,
        static_cast<double>(EqSettings::kMinQ),
        static_cast<double>(EqSettings::kMaxQ)));
}

void toggleEqFilterBypass(EqFilter& filter)
{
    filter.bypassed = !filter.bypassed;
}

void toggleEqActive(EqSettings& settings)
{
    settings.active = !settings.active;
}

} // namespace OpenTune
