#pragma once

#include "Content/ContentEditCommands.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace OpenTune {

struct EditableContentSnapshot;

enum class SelectedNoteEqEditStatus
{
    Ready,
    Applied,
    NoChange,
    NoTarget,
    StaleRevision,
    InvalidSettings,
    Failed
};

struct SelectedNoteEqEditPlan
{
    SelectedNoteEqEditStatus status = SelectedNoteEqEditStatus::NoTarget;
    ContentNoteRangePatch beforePatch;
    ContentNoteRangePatch afterPatch;
};

SelectedNoteEqEditPlan planSelectedNoteEqEdit(
    ContentKey contentKey,
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision,
    const std::optional<EqSettings>& settings);

} // namespace OpenTune
