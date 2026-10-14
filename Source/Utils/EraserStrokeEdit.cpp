#include "EraserStrokeEdit.h"
#include "Content/EditableContentSnapshot.h"
#include "PitchCurve.h"
#include <algorithm>

namespace OpenTune {

EraserStrokeEditPlan planEraserStrokeEdit(
    const EditableContentSnapshot& snapshot,
    const F0Timeline& timeline,
    F0FrameRange brushRange,
    const std::vector<int>& noteIndices,
    uint64_t expectedNotesRevision)
{
    EraserStrokeEditPlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = EraserStrokeEditStatus::StaleRevision;
        return plan;
    }
    if (snapshot.pitchCurve == nullptr) {
        plan.status = EraserStrokeEditStatus::Failed;
        return plan;
    }

    const auto& originalF0 = snapshot.pitchCurve->getOriginalF0();
    const int startFrame = std::max(0, brushRange.startFrame);
    const int endFrameExclusive = std::min(static_cast<int>(originalF0.size()),
                                           brushRange.endFrameExclusive);
    if (endFrameExclusive <= startFrame)
        return plan;

    plan.affectedRange = { startFrame, endFrameExclusive };
    plan.beforeNotes = snapshot.notes;
    std::vector<char> eraseNote(snapshot.notes.size(), 0);
    for (const int noteIndex : noteIndices) {
        if (noteIndex >= 0 && noteIndex < static_cast<int>(snapshot.notes.size()))
            eraseNote[static_cast<size_t>(noteIndex)] = 1;
    }

    plan.afterNotes.reserve(snapshot.notes.size());
    for (size_t index = 0; index < snapshot.notes.size(); ++index) {
        if (!eraseNote[index])
            plan.afterNotes.push_back(snapshot.notes[index]);
    }

    int commitStartFrame = startFrame;
    int commitEndFrameExclusive = endFrameExclusive;
    if (!timeline.isEmpty()) {
        for (const int noteIndex : noteIndices) {
            if (noteIndex < 0 || noteIndex >= static_cast<int>(snapshot.notes.size()))
                continue;
            const auto& note = snapshot.notes[static_cast<size_t>(noteIndex)];
            const auto noteRange = timeline.nonEmptyRangeForTimes(note.startTime, note.endTime);
            if (!noteRange.isEmpty()) {
                commitStartFrame = std::min(commitStartFrame, noteRange.startFrame);
                commitEndFrameExclusive = std::max(commitEndFrameExclusive,
                                                   noteRange.endFrameExclusive);
            }
        }
    }
    plan.affectedRange = { commitStartFrame, commitEndFrameExclusive };

    auto scratchCurve = PitchCurve::fromSnapshot(snapshot.pitchCurve);
    scratchCurve->clearCorrectionRange(startFrame, endFrameExclusive);
    for (const auto& segment : snapshot.pitchCurve->getCorrectionSegments()) {
        if (segment.startFrame < commitEndFrameExclusive
            && segment.endFrame > commitStartFrame)
            plan.beforeSegments.push_back(segment);
    }
    for (const auto& segment : scratchCurve->copyCorrectionSegments()) {
        if (segment.startFrame < commitEndFrameExclusive
            && segment.endFrame > commitStartFrame)
            plan.afterSegments.push_back(segment);
    }

    plan.beforeOriginalF0.assign(originalF0.begin() + commitStartFrame,
                                 originalF0.begin() + commitEndFrameExclusive);
    plan.afterOriginalF0 = plan.beforeOriginalF0;
    for (int frame = startFrame; frame < endFrameExclusive; ++frame)
        plan.afterOriginalF0[static_cast<size_t>(frame - commitStartFrame)] = 0.0f;

    for (const auto& range : snapshot.pitchCurve->getErasedRanges()) {
        const int overlapStart = std::max(range.startFrame, commitStartFrame);
        const int overlapEnd = std::min(range.endFrameExclusive, commitEndFrameExclusive);
        if (overlapStart < overlapEnd)
            plan.beforeErasedRanges.push_back({ overlapStart, overlapEnd });
    }
    plan.afterErasedRanges = plan.beforeErasedRanges;
    plan.afterErasedRanges.push_back({ startFrame, endFrameExclusive });
    plan.status = EraserStrokeEditStatus::Ready;
    return plan;
}

} // namespace OpenTune
