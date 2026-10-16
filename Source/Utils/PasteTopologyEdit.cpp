#include "PasteTopologyEdit.h"
#include "Content/EditableContentSnapshot.h"

namespace OpenTune {

PasteNotesEditPlan planPasteNotesEdit(
    const EditableContentSnapshot& snapshot,
    const std::vector<Note>& clipboardNotes,
    double pasteStartTime,
    uint64_t expectedNotesRevision)
{
    PasteNotesEditPlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = PasteNotesStatus::StaleRevision;
        return plan;
    }
    if (clipboardNotes.empty())
        return plan;

    double clipMinTime = clipboardNotes.front().startTime;
    for (const auto& n : clipboardNotes)
        clipMinTime = std::min(clipMinTime, n.startTime);

    plan.pastedNotes.reserve(clipboardNotes.size());
    for (auto n : clipboardNotes) {
        const double duration = n.endTime - n.startTime;
        n.startTime = pasteStartTime + (n.startTime - clipMinTime);
        n.endTime = n.startTime + duration;
        n.dirty = true;
        plan.pastedNotes.push_back(n);
    }

    const auto topology = planPasteTopology(snapshot.notes, plan.pastedNotes);
    plan.beforePatch.affectedRange = topology.affectedRange;
    plan.beforePatch.afterNotesInRange = topology.beforeNotesInRange;
    plan.afterPatch.affectedRange = topology.affectedRange;
    plan.afterPatch.afterNotesInRange = topology.afterNotesInRange;
    plan.status = PasteNotesStatus::Ready;
    return plan;
}

} // namespace OpenTune
