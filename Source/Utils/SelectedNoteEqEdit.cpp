#include "SelectedNoteEqEdit.h"
#include "Content/EditableContentSnapshot.h"
#include <algorithm>

namespace OpenTune {

SelectedNoteEqEditPlan planSelectedNoteEqEdit(
    ContentKey contentKey,
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision,
    const std::optional<EqSettings>& settings)
{
    SelectedNoteEqEditPlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = SelectedNoteEqEditStatus::StaleRevision;
        return plan;
    }
    if (!contentKey.isValid() || selectedNoteIndices.empty())
        return plan;

    int validTargetCount = 0;
    for (const int index : selectedNoteIndices) {
        if (index >= 0 && index < static_cast<int>(snapshot.notes.size()))
            ++validTargetCount;
    }
    if (validTargetCount == 0 || validTargetCount != static_cast<int>(selectedNoteIndices.size()))
        return plan;
    if (settings && !settings->isValid()) {
        plan.status = SelectedNoteEqEditStatus::InvalidSettings;
        return plan;
    }

    auto editedNotes = snapshot.notes;
    double affectedStart = 0.0;
    double affectedEnd = 0.0;
    bool changed = false;
    for (const int index : selectedNoteIndices) {
        auto& note = editedNotes[static_cast<size_t>(index)];
        if (note.eq == settings)
            continue;

        if (!changed) {
            affectedStart = note.startTime;
            affectedEnd = note.endTime;
            changed = true;
        } else {
            affectedStart = std::min(affectedStart, note.startTime);
            affectedEnd = std::max(affectedEnd, note.endTime);
        }
        note.eq = settings;
    }

    if (!changed) {
        plan.status = SelectedNoteEqEditStatus::NoChange;
        return plan;
    }

    const NoteRangeSeconds affectedRange{ affectedStart, affectedEnd };
    const auto inRange = [affectedStart, affectedEnd](const Note& note) {
        return note.endTime > affectedStart && note.startTime < affectedEnd;
    };
    plan.beforePatch.affectedRange = affectedRange;
    plan.afterPatch.affectedRange = affectedRange;
    for (const auto& note : snapshot.notes) {
        if (inRange(note))
            plan.beforePatch.afterNotesInRange.push_back(note);
    }
    for (const auto& note : editedNotes) {
        if (inRange(note))
            plan.afterPatch.afterNotesInRange.push_back(note);
    }
    plan.status = SelectedNoteEqEditStatus::Ready;
    return plan;
}

} // namespace OpenTune
