#pragma once

#include "Content/ContentEditCommands.h"
#include "F0Timeline.h"
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace OpenTune {

struct EditableContentSnapshot;

enum class SelectedNoteDeleteStatus
{
    Ready,
    Applied,
    NoTarget,
    StaleRevision,
    InvalidSelection,
    Failed
};

struct SelectedNoteTopologyDelete
{
    ContentNoteRangePatch beforePatch;
    ContentNoteRangePatch afterPatch;
};

struct SelectedNotePitchCurveDelete
{
    F0FrameRange affectedRange;
    std::vector<Note> beforeNotes;
    std::vector<Note> afterNotes;
    std::vector<PitchCorrectionSegment> beforeSegments;
    std::vector<PitchCorrectionSegment> afterSegments;
};

struct SelectedNoteDeletePlan
{
    SelectedNoteDeleteStatus status = SelectedNoteDeleteStatus::NoTarget;
    std::variant<std::monostate, SelectedNoteTopologyDelete, SelectedNotePitchCurveDelete> edit;
};

struct SelectedNoteDeleteResult
{
    SelectedNoteDeleteStatus status = SelectedNoteDeleteStatus::Failed;
    ContentCommitSnapshot contentSnapshot;
    std::vector<Note> beforeNotes;
    F0FrameRange affectedRange;
    bool pitchCurveEdited = false;
};

SelectedNoteDeletePlan planSelectedNoteDelete(
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision);

} // namespace OpenTune
