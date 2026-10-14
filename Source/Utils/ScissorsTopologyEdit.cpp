#include "ScissorsTopologyEdit.h"
#include <limits>

namespace OpenTune {

float avgEffectiveF0InRange(const F0Timeline& f0tl,
                            const std::vector<float>& effectiveF0,
                            double tStart,
                            double tEnd)
{
    if (effectiveF0.empty())
        return 0.0f;

    const int startFrame = f0tl.frameAtOrBefore(tStart);
    const int endFrame = f0tl.exclusiveFrameAt(tEnd);
    float sum = 0.0f;
    int count = 0;
    for (int f = startFrame; f < endFrame; ++f) {
        if (f < 0 || f >= static_cast<int>(effectiveF0.size()))
            continue;
        const float f0 = effectiveF0[static_cast<std::size_t>(f)];
        if (f0 > 0.0f) {
            sum += f0;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

namespace {

// pitch center 重算：avg > 0 时设为均值并清零 pitchOffset；avg <= 0 保持原 pitch。
void applyAvgPitch(Note& note, float avgF0)
{
    if (avgF0 > 0.0f) {
        note.pitch = avgF0;
        note.pitchOffset = 0.0f;
    }
}

} // namespace

ScissorsSplitPlan planScissorsSplit(const std::vector<Note>& beforeNotes,
                                    double splitTime,
                                    const std::vector<float>& effectiveF0,
                                    const F0Timeline& f0tl)
{
    ScissorsSplitPlan plan;

    auto isCut = [&](const Note& note) {
        return note.startTime < splitTime && splitTime < note.endTime;
    };
    for (const auto& note : beforeNotes) {
        if (isCut(note))
            ++plan.cutCount;
    }
    if (plan.cutCount == 0) {
        plan.afterNotes = beforeNotes;
        return plan;
    }

    const bool hasF0 = !effectiveF0.empty();
    plan.afterNotes.reserve(beforeNotes.size() + static_cast<size_t>(plan.cutCount));

    for (const auto& original : beforeNotes) {
        if (!isCut(original)) {
            plan.afterNotes.push_back(original);
            continue;
        }

        Note left = original;
        Note right = original;
        left.endTime = splitTime;
        left.dirty = true;
        right.startTime = splitTime;
        right.dirty = true;

        if (hasF0) {
            applyAvgPitch(left, avgEffectiveF0InRange(f0tl, effectiveF0, original.startTime, splitTime));
            applyAvgPitch(right, avgEffectiveF0InRange(f0tl, effectiveF0, splitTime, original.endTime));
        }

        plan.lastRightIndex = static_cast<int>(plan.afterNotes.size()) + 1;
        plan.afterNotes.push_back(left);
        plan.afterNotes.push_back(right);
    }
    return plan;
}

ScissorsMergePlan planScissorsMerge(const std::vector<Note>& beforeNotes,
                                    double splitTime,
                                    double tolerance,
                                    const std::vector<float>& effectiveF0,
                                    const F0Timeline& f0tl)
{
    ScissorsMergePlan plan;
    const bool hasF0 = !effectiveF0.empty();

    std::vector<char> consumed(beforeNotes.size(), 0);
    plan.afterNotes.reserve(beforeNotes.size());

    for (size_t i = 0; i < beforeNotes.size(); ++i) {
        if (consumed[i] != 0)
            continue;
        const Note& left = beforeNotes[i];
        if (std::abs(left.endTime - splitTime) >= tolerance) {
            plan.afterNotes.push_back(left);
            continue;
        }

        size_t rightIdx = std::numeric_limits<size_t>::max();
        for (size_t j = i + 1; j < beforeNotes.size(); ++j) {
            if (consumed[j] != 0)
                continue;
            if (beforeNotes[j].startTime > splitTime + tolerance)
                break;
            if (std::abs(beforeNotes[j].startTime - splitTime) < tolerance) {
                rightIdx = j;
                break;
            }
        }
        if (rightIdx == std::numeric_limits<size_t>::max()) {
            plan.afterNotes.push_back(left);
            continue;
        }

        consumed[rightIdx] = 1;
        const Note& right = beforeNotes[rightIdx];
        Note merged = left;
        merged.endTime = right.endTime;
        merged.dirty = true;
        if (hasF0)
            applyAvgPitch(merged, avgEffectiveF0InRange(f0tl, effectiveF0, left.startTime, right.endTime));
        plan.afterNotes.push_back(merged);
        ++plan.mergeCount;
    }
    return plan;
}

} // namespace OpenTune
