#pragma once

#include "Content/ContentEditCommands.h"
#include "F0Timeline.h"
#include <cstdint>
#include <vector>

namespace OpenTune {

struct EditableContentSnapshot;

enum class EraserStrokeEditStatus
{
    Ready,
    Applied,
    StaleRevision,
    InvalidRange,
    Failed
};

struct EraserStrokeEditPlan
{
    EraserStrokeEditStatus status = EraserStrokeEditStatus::InvalidRange;
    F0FrameRange affectedRange;
    std::vector<Note> beforeNotes;
    std::vector<Note> afterNotes;
    std::vector<PitchCorrectionSegment> beforeSegments;
    std::vector<PitchCorrectionSegment> afterSegments;
    std::vector<float> beforeOriginalF0;
    std::vector<float> afterOriginalF0;
    std::vector<F0FrameRange> beforeErasedRanges;
    std::vector<F0FrameRange> afterErasedRanges;
};

struct EraserStrokeEditResult
{
    EraserStrokeEditStatus status = EraserStrokeEditStatus::Failed;
    ContentCommitSnapshot contentSnapshot;
    std::vector<Note> beforeNotes;
    F0FrameRange affectedRange;
};

EraserStrokeEditPlan planEraserStrokeEdit(
    const EditableContentSnapshot& snapshot,
    const F0Timeline& timeline,
    F0FrameRange brushRange,
    const std::vector<int>& noteIndices,
    uint64_t expectedNotesRevision);

} // namespace OpenTune
