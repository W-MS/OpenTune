#include "NoteResizeEdit.h"

#include <algorithm>

namespace OpenTune {

NoteResizeEditPlan planNoteResizeEdit(const std::vector<Note>& notes,
                                      int noteIndex,
                                      NoteResizeEdge edge,
                                      double clampedTime,
                                      double minDuration)
{
    NoteResizeEditPlan plan;
    plan.afterNotes = notes;
    if (noteIndex < 0 || noteIndex >= static_cast<int>(plan.afterNotes.size())
        || edge == NoteResizeEdge::None)
        return plan;

    const auto index = static_cast<std::size_t>(noteIndex);
    const Note& originalTarget = notes[index];
    auto& target = plan.afterNotes[index];
    plan.dirtyStartTime = originalTarget.startTime;
    plan.dirtyEndTime = originalTarget.endTime;

    if (edge == NoteResizeEdge::Left) {
        double newStart = std::min(clampedTime, target.endTime - minDuration);
        newStart = std::max(0.0, newStart);
        if (noteIndex > 0) {
            const auto& prev = plan.afterNotes[index - 1];
            if (newStart < prev.endTime) {
                const double oldPrevEnd = prev.endTime;
                newStart = std::max(newStart, std::min(prev.endTime, prev.startTime + minDuration));
                if (newStart < prev.endTime) {
                    auto& yielded = plan.afterNotes[index - 1];
                    yielded.endTime = newStart;
                    yielded.dirty = true;
                    plan.dirtyStartTime = std::min(plan.dirtyStartTime, yielded.startTime);
                    plan.dirtyEndTime = std::max(plan.dirtyEndTime, oldPrevEnd);
                }
            }
        }
        target.startTime = newStart;
        plan.dirtyStartTime = std::min(plan.dirtyStartTime, target.startTime);
    } else if (edge == NoteResizeEdge::Right) {
        double newEnd = std::max(clampedTime, target.startTime + minDuration);
        if (noteIndex + 1 < static_cast<int>(plan.afterNotes.size())) {
            const auto& next = plan.afterNotes[index + 1];
            if (newEnd > next.startTime) {
                const double oldNextStart = next.startTime;
                newEnd = std::min(newEnd, std::max(next.startTime, next.endTime - minDuration));
                if (newEnd > next.startTime) {
                    auto& yielded = plan.afterNotes[index + 1];
                    yielded.startTime = newEnd;
                    yielded.dirty = true;
                    plan.dirtyStartTime = std::min(plan.dirtyStartTime, oldNextStart);
                    plan.dirtyEndTime = std::max(plan.dirtyEndTime, yielded.endTime);
                }
            }
        }
        target.endTime = newEnd;
        plan.dirtyEndTime = std::max(plan.dirtyEndTime, target.endTime);
    }

    target.dirty = true;
    return plan;
}

} // namespace OpenTune
