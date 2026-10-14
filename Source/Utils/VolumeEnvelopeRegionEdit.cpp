#include "VolumeEnvelopeRegionEdit.h"
#include "Content/EditableContentSnapshot.h"
#include <algorithm>

namespace OpenTune {

AutomationLane applyVolumeEnvelopeRegionDelta(const AutomationLane& beforeEnvelope,
                                              const std::vector<Note>& notes,
                                              const std::vector<int>& selectedNoteIndices,
                                              float deltaGainDb)
{
    AutomationLane envelope = beforeEnvelope;
    for (const int index : selectedNoteIndices) {
        if (index < 0 || index >= static_cast<int>(notes.size()))
            continue;
        const auto& note = notes[static_cast<size_t>(index)];
        envelope.setRegionGain(
            note.startTime,
            note.endTime,
            beforeEnvelope.evalAt(note.startTime) + deltaGainDb);
    }
    return envelope;
}

AutomationLane applyVolumeEnvelopeRegionReset(const AutomationLane& beforeEnvelope,
                                              const std::vector<Note>& notes,
                                              int noteIndex)
{
    AutomationLane envelope = beforeEnvelope;
    if (noteIndex < 0 || noteIndex >= static_cast<int>(notes.size()))
        return envelope;
    const auto& note = notes[static_cast<size_t>(noteIndex)];
    envelope.setRegionGain(note.startTime, note.endTime, 0.0f);
    return envelope;
}

VolumeEnvelopeRegionEditPlan planVolumeEnvelopeRegionDeltaEdit(
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision,
    float deltaGainDb)
{
    VolumeEnvelopeRegionEditPlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = VolumeEnvelopeRegionEditStatus::StaleRevision;
        return plan;
    }
    const bool hasValidTargets = !selectedNoteIndices.empty() && std::all_of(
        selectedNoteIndices.begin(), selectedNoteIndices.end(),
        [&snapshot](int index) {
            return index >= 0 && index < static_cast<int>(snapshot.notes.size());
        });
    if (!hasValidTargets)
        return plan;

    plan.beforeEnvelope = snapshot.volumeEnvelope;
    plan.afterEnvelope = applyVolumeEnvelopeRegionDelta(
        snapshot.volumeEnvelope, snapshot.notes, selectedNoteIndices, deltaGainDb);
    plan.status = plan.afterEnvelope == plan.beforeEnvelope
        ? VolumeEnvelopeRegionEditStatus::NoChange
        : VolumeEnvelopeRegionEditStatus::Ready;
    return plan;
}

VolumeEnvelopeRegionEditPlan planVolumeEnvelopeRegionResetEdit(
    const EditableContentSnapshot& snapshot,
    int noteIndex,
    uint64_t expectedNotesRevision)
{
    VolumeEnvelopeRegionEditPlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = VolumeEnvelopeRegionEditStatus::StaleRevision;
        return plan;
    }
    if (noteIndex < 0 || noteIndex >= static_cast<int>(snapshot.notes.size()))
        return plan;

    plan.beforeEnvelope = snapshot.volumeEnvelope;
    plan.afterEnvelope = applyVolumeEnvelopeRegionReset(
        snapshot.volumeEnvelope, snapshot.notes, noteIndex);
    plan.status = plan.afterEnvelope == plan.beforeEnvelope
        ? VolumeEnvelopeRegionEditStatus::NoChange
        : VolumeEnvelopeRegionEditStatus::Ready;
    return plan;
}

} // namespace OpenTune
