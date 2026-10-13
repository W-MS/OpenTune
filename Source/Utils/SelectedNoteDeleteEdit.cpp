#include "SelectedNoteDeleteEdit.h"
#include "Content/ContentPatchGeometry.h"
#include "Content/EditableContentSnapshot.h"
#include "PitchCurve.h"
#include <algorithm>
#include <utility>

namespace OpenTune {
namespace {

SelectedNoteTopologyDelete makeTopologyDelete(
    const std::vector<Note>& beforeNotes,
    const std::vector<Note>& afterNotes,
    double startSeconds,
    double endSeconds)
{
    SelectedNoteTopologyDelete edit;
    edit.beforePatch.affectedRange = { startSeconds, endSeconds };
    edit.afterPatch.affectedRange = edit.beforePatch.affectedRange;
    edit.beforePatch.afterNotesInRange = filterNotesToRange(beforeNotes, startSeconds, endSeconds);
    edit.afterPatch.afterNotesInRange = filterNotesToRange(afterNotes, startSeconds, endSeconds);
    return edit;
}

} // namespace

SelectedNoteDeletePlan planSelectedNoteDelete(
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision)
{
    SelectedNoteDeletePlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = SelectedNoteDeleteStatus::StaleRevision;
        return plan;
    }
    if (selectedNoteIndices.empty())
        return plan;

    for (const int index : selectedNoteIndices) {
        if (index < 0 || static_cast<size_t>(index) >= snapshot.notes.size()) {
            plan.status = SelectedNoteDeleteStatus::InvalidSelection;
            return plan;
        }
    }

    std::vector<char> deleteMask(snapshot.notes.size(), 0);
    double startSeconds = snapshot.notes[static_cast<size_t>(selectedNoteIndices.front())].startTime;
    double endSeconds = snapshot.notes[static_cast<size_t>(selectedNoteIndices.front())].endTime;
    for (const int index : selectedNoteIndices) {
        deleteMask[static_cast<size_t>(index)] = 1;
        const auto& note = snapshot.notes[static_cast<size_t>(index)];
        startSeconds = std::min(startSeconds, note.startTime);
        endSeconds = std::max(endSeconds, note.endTime);
    }

    std::vector<Note> afterNotes;
    for (size_t index = 0; index < snapshot.notes.size(); ++index) {
        if (!deleteMask[index])
            afterNotes.push_back(snapshot.notes[index]);
    }

    if (snapshot.pitchCurve != nullptr) {
        const auto& curveSnapshot = snapshot.pitchCurve;
        const F0Timeline timeline(curveSnapshot->getHopSize(), curveSnapshot->getSampleRate(),
                                  static_cast<int>(curveSnapshot->size()));
        std::vector<F0FrameRange> correctionClearRanges;
        if (!timeline.isEmpty()) {
            for (const int index : selectedNoteIndices) {
                const auto& note = snapshot.notes[static_cast<size_t>(index)];
                const auto range = timeline.rangeForTimes(note.startTime, note.endTime);
                if (!range.isEmpty())
                    correctionClearRanges.push_back(range);
            }
        }

        std::sort(correctionClearRanges.begin(), correctionClearRanges.end(),
                  [](const F0FrameRange& left, const F0FrameRange& right) {
                      return left.startFrame < right.startFrame;
                  });
        std::vector<F0FrameRange> mergedRanges;
        for (const auto& range : correctionClearRanges) {
            if (!mergedRanges.empty()
                && range.startFrame <= mergedRanges.back().endFrameExclusive) {
                mergedRanges.back().endFrameExclusive =
                    std::max(mergedRanges.back().endFrameExclusive, range.endFrameExclusive);
            } else {
                mergedRanges.push_back(range);
            }
        }

        if (!mergedRanges.empty()) {
            const F0FrameRange affectedRange{
                mergedRanges.front().startFrame,
                mergedRanges.back().endFrameExclusive
            };
            auto scratchCurve = PitchCurve::fromSnapshot(curveSnapshot);
            auto clearedSnapshot = curveSnapshot;
            for (const auto& range : mergedRanges)
                clearedSnapshot = scratchCurve->clearCorrectionRange(range.startFrame, range.endFrameExclusive);

            const double affectedStartSeconds = timeline.timeAtFrame(affectedRange.startFrame);
            const double affectedEndSeconds = timeline.timeAtFrame(affectedRange.endFrameExclusive);
            SelectedNotePitchCurveDelete edit;
            edit.affectedRange = affectedRange;
            edit.beforeNotes = filterNotesToRange(snapshot.notes, affectedStartSeconds, affectedEndSeconds);
            edit.afterNotes = filterNotesToRange(afterNotes, affectedStartSeconds, affectedEndSeconds);
            edit.beforeSegments = clipSegmentsToFrameRange(
                curveSnapshot->getCorrectionSegments(),
                affectedRange.startFrame, affectedRange.endFrameExclusive);
            edit.afterSegments = clipSegmentsToFrameRange(
                clearedSnapshot->getCorrectionSegments(),
                affectedRange.startFrame, affectedRange.endFrameExclusive);
            plan.status = SelectedNoteDeleteStatus::Ready;
            plan.edit = std::move(edit);
            return plan;
        }
    }

    plan.status = SelectedNoteDeleteStatus::Ready;
    plan.edit = makeTopologyDelete(snapshot.notes, afterNotes, startSeconds, endSeconds);
    return plan;
}

} // namespace OpenTune
