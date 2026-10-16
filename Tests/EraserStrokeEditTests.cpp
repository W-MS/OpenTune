#include "Content/EditableContentSnapshot.h"
#include "Utils/EraserStrokeEdit.h"
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace OpenTune;

EditableContentSnapshot makeSnapshot()
{
    EditableContentSnapshot snapshot;
    snapshot.notesRevision = 12;

    Note erased;
    erased.startTime = 0.4;
    erased.endTime = 0.9;
    Note retained;
    retained.startTime = 1.1;
    retained.endTime = 1.4;
    snapshot.notes = { erased, retained };

    PitchCorrectionSegment correction;
    correction.startFrame = 2;
    correction.endFrame = 12;
    correction.f0Data = { 102.0f, 103.0f, 104.0f, 105.0f, 106.0f,
                          107.0f, 108.0f, 109.0f, 110.0f, 111.0f };
    PitchCorrectionSegment outside;
    outside.startFrame = 12;
    outside.endFrame = 14;
    outside.f0Data = { 112.0f, 113.0f };
    std::vector<float> originalF0;
    for (int frame = 0; frame < 16; ++frame)
        originalF0.push_back(static_cast<float>(100 + frame));
    snapshot.pitchCurve = std::make_shared<const PitchCurveSnapshot>(
        std::move(originalF0), std::vector<float>{},
        std::vector<PitchCorrectionSegment>{ correction, outside }, 1, 10.0, 0,
        std::vector<F0FrameRange>{ { 1, 6 }, { 8, 11 }, { 13, 15 } });
    return snapshot;
}

bool checkStrokePlanSemantics()
{
    const auto snapshot = makeSnapshot();
    const F0Timeline timeline{ 1, 10.0, 16 };
    const auto plan = planEraserStrokeEdit(snapshot, timeline, { 6, 8 }, { 0 }, 12);
    if (plan.status != EraserStrokeEditStatus::Ready
        || plan.affectedRange.startFrame != 4
        || plan.affectedRange.endFrameExclusive != 9)
        return false;

    if (plan.beforeNotes.size() != 2 || plan.afterNotes.size() != 1
        || plan.afterNotes[0].startTime != 1.1
        || plan.afterNotes[0].endTime != 1.4)
        return false;

    const std::vector<float> expectedBeforeF0{ 104.0f, 105.0f, 106.0f, 107.0f, 108.0f };
    const std::vector<float> expectedAfterF0{ 104.0f, 105.0f, 0.0f, 0.0f, 108.0f };
    if (plan.beforeOriginalF0 != expectedBeforeF0
        || plan.afterOriginalF0 != expectedAfterF0)
        return false;

    if (plan.beforeSegments.size() != 1
        || plan.beforeSegments[0].startFrame != 2
        || plan.beforeSegments[0].endFrame != 12
        || plan.afterSegments.size() != 2
        || plan.afterSegments[0].startFrame != 2
        || plan.afterSegments[0].endFrame != 6
        || plan.afterSegments[1].startFrame != 8
        || plan.afterSegments[1].endFrame != 12)
        return false;
    if (plan.afterSegments[0].f0Data != std::vector<float>{ 102.0f, 103.0f, 104.0f, 105.0f }
        || plan.afterSegments[1].f0Data != std::vector<float>{ 108.0f, 109.0f, 110.0f, 111.0f })
        return false;

    const std::vector<F0FrameRange> expectedBeforeErased{ { 4, 6 }, { 8, 9 } };
    const std::vector<F0FrameRange> expectedAfterErased{ { 4, 6 }, { 8, 9 }, { 6, 8 } };
    if (plan.beforeErasedRanges.size() != expectedBeforeErased.size()
        || plan.afterErasedRanges.size() != expectedAfterErased.size())
        return false;
    for (size_t index = 0; index < expectedBeforeErased.size(); ++index) {
        if (plan.beforeErasedRanges[index].startFrame != expectedBeforeErased[index].startFrame
            || plan.beforeErasedRanges[index].endFrameExclusive != expectedBeforeErased[index].endFrameExclusive)
            return false;
    }
    for (size_t index = 0; index < expectedAfterErased.size(); ++index) {
        if (plan.afterErasedRanges[index].startFrame != expectedAfterErased[index].startFrame
            || plan.afterErasedRanges[index].endFrameExclusive != expectedAfterErased[index].endFrameExclusive)
            return false;
    }
    return true;
}

bool checkBrushOnlyAndInvalidRanges()
{
    const auto snapshot = makeSnapshot();
    const F0Timeline timeline{ 1, 10.0, 16 };
    const auto brushOnly = planEraserStrokeEdit(snapshot, timeline, { 6, 8 }, {}, 12);
    if (brushOnly.status != EraserStrokeEditStatus::Ready
        || brushOnly.affectedRange.startFrame != 6
        || brushOnly.affectedRange.endFrameExclusive != 8
        || brushOnly.afterNotes.size() != snapshot.notes.size()
        || brushOnly.afterOriginalF0 != std::vector<float>{ 0.0f, 0.0f })
        return false;

    const auto empty = planEraserStrokeEdit(snapshot, timeline, { 6, 6 }, { 0 }, 12);
    const auto reversed = planEraserStrokeEdit(snapshot, timeline, { 8, 6 }, { 0 }, 12);
    const auto outside = planEraserStrokeEdit(snapshot, timeline, { 20, 22 }, { 0 }, 12);
    return empty.status == EraserStrokeEditStatus::InvalidRange
        && reversed.status == EraserStrokeEditStatus::InvalidRange
        && outside.status == EraserStrokeEditStatus::InvalidRange;
}

bool checkStaleRevisionAndInvalidIndices()
{
    const auto snapshot = makeSnapshot();
    const F0Timeline timeline{ 1, 10.0, 16 };
    const auto stale = planEraserStrokeEdit(snapshot, timeline, { 6, 8 }, { 0 }, 11);
    if (stale.status != EraserStrokeEditStatus::StaleRevision)
        return false;

    const auto valid = planEraserStrokeEdit(snapshot, timeline, { 6, 8 }, { -1, 99, 0, 0 }, 12);
    return valid.status == EraserStrokeEditStatus::Ready
        && valid.afterNotes.size() == 1
        && valid.afterNotes[0].startTime == 1.1;
}

} // namespace

int main()
{
    if (!checkStrokePlanSemantics()) {
        std::cerr << "Eraser stroke plan semantics check failed\n";
        return 1;
    }
    if (!checkBrushOnlyAndInvalidRanges()) {
        std::cerr << "Eraser brush-only or range status check failed\n";
        return 1;
    }
    if (!checkStaleRevisionAndInvalidIndices()) {
        std::cerr << "Eraser stale revision or index check failed\n";
        return 1;
    }
    return 0;
}
