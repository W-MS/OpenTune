#pragma once

#include "../Content/ContentEditCommands.h"
#include "../Content/EditableContentSnapshot.h"
#include "../Content/ContentPatchGeometry.h"

namespace OpenTune {

struct DrawNoteEditInput
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float pitch = 0.0f;
    float retuneSpeed = 0.0f;
    float vibratoDepth = 0.0f;
    float vibratoRate = 0.0f;
};

enum class DrawNotePlanStatus
{
    Ready,
    NoChange,
    StaleRevision
};

struct DrawNoteEditPlan
{
    DrawNotePlanStatus status = DrawNotePlanStatus::NoChange;
    Note insertedNote;
    std::vector<Note> afterNotes;
    ContentNoteRangePatch beforePatch;
    ContentNoteRangePatch afterPatch;
    bool pitchCurveEdited = false;
    F0FrameRange affectedRange;
    std::vector<Note> beforeNotesInPitchRange;
    std::vector<PitchCorrectionSegment> beforeSegments;
    std::vector<PitchCorrectionSegment> segmentsToCommit;
};

enum class DrawNoteCommitStatus
{
    Failed,
    StaleRevision,
    NoChange,
    Applied
};

struct DrawNoteCommitResult
{
    DrawNoteCommitStatus status = DrawNoteCommitStatus::Failed;
    ContentCommitSnapshot contentSnapshot;
    Note insertedNote;
    bool pitchCurveEdited = false;
    F0FrameRange affectedRange;
};

DrawNoteEditPlan planDrawNoteEdit(const EditableContentSnapshot& snapshot,
                                  const DrawNoteEditInput& input,
                                  uint64_t expectedNotesRevision);

} // namespace OpenTune
