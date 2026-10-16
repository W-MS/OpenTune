#include "Utils/ManualCorrectionEdit.h"
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace OpenTune;

bool near(float actual, float expected, float tolerance = 0.001f)
{
    return std::abs(actual - expected) <= tolerance;
}

bool checkEditableGapsAndOperationMetadata()
{
    const std::vector<float> originalF0{ 0.0f, 100.0f, 110.0f, 0.0f, 120.0f,
                                         130.0f, 140.0f, 150.0f, 160.0f, 170.0f, 0.0f };
    const std::vector<float> drawnF0{ 90.0f, 201.0f, 202.0f, 203.0f, 0.0f,
                                      205.0f, -1.0f, 207.0f, 208.0f, 209.0f, 210.0f };
    std::vector<int> queriedFrames;
    PitchCorrectionSegment::ParameterSnapshot parameters;
    parameters.retuneSpeed = 0.25f;
    parameters.vibratoDepth = 0.4f;
    parameters.vibratoRate = 6.0f;
    parameters.pitchDriftScale = 0.7f;

    const auto ops = planManualCorrectionOps(
        AudioEditingScheme::Scheme::NotesPrimary,
        originalF0,
        { 0, 11 },
        [&](int frame) {
            queriedFrames.push_back(frame);
            return drawnF0[static_cast<std::size_t>(frame)];
        },
        PitchCorrectionSegment::Source::HandDraw,
        parameters);

    if (ops.size() != 3
        || ops[0].startFrame != 1 || ops[0].endFrameExclusive != 3
        || ops[0].f0Data != std::vector<float>{ 201.0f, 202.0f }
        || ops[1].startFrame != 5 || ops[1].endFrameExclusive != 6
        || ops[1].f0Data != std::vector<float>{ 205.0f }
        || ops[2].startFrame != 7 || ops[2].endFrameExclusive != 10
        || ops[2].f0Data != std::vector<float>{ 207.0f, 208.0f, 209.0f })
        return false;

    const std::vector<int> expectedQueries{ 1, 2, 4, 5, 6, 7, 8, 9 };
    if (queriedFrames != expectedQueries)
        return false;

    for (const auto& op : ops) {
        if (op.source != PitchCorrectionSegment::Source::HandDraw
            || !near(op.parameterSnapshot.retuneSpeed, parameters.retuneSpeed)
            || !near(op.parameterSnapshot.vibratoDepth, parameters.vibratoDepth)
            || !near(op.parameterSnapshot.vibratoRate, parameters.vibratoRate)
            || !near(op.parameterSnapshot.pitchDriftScale, parameters.pitchDriftScale))
            return false;
    }
    return true;
}

bool checkCorrectedF0SchemeKeepsInteriorGaps()
{
    const std::vector<float> originalF0{ 0.0f, 100.0f, 110.0f, 0.0f, 120.0f,
                                         130.0f, 140.0f, 150.0f, 160.0f, 170.0f, 0.0f };
    const auto ops = planManualCorrectionOps(
        AudioEditingScheme::Scheme::CorrectedF0Primary,
        originalF0,
        { -4, 20 },
        [](int frame) { return static_cast<float>(200 + frame); },
        PitchCorrectionSegment::Source::LineAnchor);
    return ops.size() == 2
        && ops[0].startFrame == 1 && ops[0].endFrameExclusive == 3
        && ops[1].startFrame == 4 && ops[1].endFrameExclusive == 10;
}

bool checkLogFrequencyInterpolation()
{
    return near(interpolateLogF0(100.0f, 400.0f, 0.0f), 100.0f)
        && near(interpolateLogF0(100.0f, 400.0f, 0.5f), 200.0f)
        && near(interpolateLogF0(100.0f, 400.0f, 1.0f), 400.0f)
        && near(interpolateLogF0(0.0f, 16.0f, 0.5f), 4.0f);
}

bool checkSourceTrendFitIgnoresUnvoicedFrames()
{
    const std::vector<float> sourceF0{ 100.0f, 0.0f, 400.0f, 0.0f };
    const auto fit = fitOriginalF0LogTrend(sourceF0, 0, 4);
    if (!near(fit.slope, 1.0f)
        || !near(fit.intercept, std::log2(100.0f)))
        return false;

    const auto oneSample = fitOriginalF0LogTrend({ 0.0f, 256.0f, 0.0f }, 0, 3);
    const auto noSamples = fitOriginalF0LogTrend({ 0.0f, 0.0f }, 0, 2);
    return near(oneSample.intercept, 8.0f)
        && near(oneSample.slope, 0.0f)
        && near(noSamples.intercept, 0.0f)
        && near(noSamples.slope, 0.0f);
}

bool checkLineAnchorSourceShapeAtRetuneEndpoints()
{
    const std::vector<float> originalF0{ 0.0f, 4.0f, 1.0f };
    const ManualCorrectionLogLineFit trend{};
    const float fullyShaped = lineAnchorF0WithSourceShape(
        originalF0, 1, 0, 2, 100.0f, 100.0f, 0.0f, trend);
    const float fullyRetuned = lineAnchorF0WithSourceShape(
        originalF0, 1, 0, 2, 100.0f, 100.0f, 1.0f, trend);
    const float halfRetuned = lineAnchorF0WithSourceShape(
        originalF0, 1, 0, 2, 100.0f, 100.0f, 0.5f, trend);
    const float unvoiced = lineAnchorF0WithSourceShape(
        originalF0, 0, 0, 2, 100.0f, 100.0f, 0.0f, { 8.0f, 0.0f });
    return near(fullyShaped, 400.0f)
        && near(fullyRetuned, 100.0f)
        && near(halfRetuned, 200.0f)
        && near(unvoiced, 100.0f);
}

bool checkEmptyPlans()
{
    const auto emptySource = planManualCorrectionOps(
        AudioEditingScheme::Scheme::NotesPrimary, {}, { 0, 10 },
        [](int) { return 100.0f; }, PitchCorrectionSegment::Source::HandDraw);
    const auto emptyRange = planManualCorrectionOps(
        AudioEditingScheme::Scheme::CorrectedF0Primary, { 100.0f, 110.0f }, { 1, 1 },
        [](int) { return 100.0f; }, PitchCorrectionSegment::Source::HandDraw);
    return emptySource.empty() && emptyRange.empty();
}

} // namespace

int main()
{
    if (!checkEditableGapsAndOperationMetadata()) {
        std::cerr << "Manual correction editable-gap plan check failed\n";
        return 1;
    }
    if (!checkCorrectedF0SchemeKeepsInteriorGaps()) {
        std::cerr << "Manual correction CorrectedF0 gap plan check failed\n";
        return 1;
    }
    if (!checkLogFrequencyInterpolation()) {
        std::cerr << "Manual correction logarithmic interpolation check failed\n";
        return 1;
    }
    if (!checkSourceTrendFitIgnoresUnvoicedFrames()) {
        std::cerr << "Manual correction source trend fit check failed\n";
        return 1;
    }
    if (!checkLineAnchorSourceShapeAtRetuneEndpoints()) {
        std::cerr << "Manual correction line-anchor shape check failed\n";
        return 1;
    }
    if (!checkEmptyPlans()) {
        std::cerr << "Manual correction empty-plan check failed\n";
        return 1;
    }
    return 0;
}
