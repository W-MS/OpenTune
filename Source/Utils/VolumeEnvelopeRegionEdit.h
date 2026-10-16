#pragma once

#include "AutomationLane.h"
#include "Note.h"
#include "Content/ContentEditCommands.h"
#include <cstdint>
#include <vector>

namespace OpenTune {

struct EditableContentSnapshot;

enum class VolumeEnvelopeRegionEditStatus
{
    Ready,
    Applied,
    NoChange,
    NoTarget,
    StaleRevision,
    Failed
};

// 区域增益事务计划：before/after 完整 AutomationLane。
// after 由选中音符区域顺序 setRegionGain 得到（区域重叠时每次仍以 before 为基准取 evalAt）。
struct VolumeEnvelopeRegionEditPlan
{
    VolumeEnvelopeRegionEditStatus status = VolumeEnvelopeRegionEditStatus::NoTarget;
    AutomationLane beforeEnvelope;
    AutomationLane afterEnvelope;
};

// Processor 消息线程事务结果（UI 经 Context 回调消费）。
struct VolumeEnvelopeRegionEditResult
{
    VolumeEnvelopeRegionEditStatus status = VolumeEnvelopeRegionEditStatus::Failed;
    ContentCommitSnapshot contentSnapshot;
};

// 顺序施加区域增益：每个选中 note setRegionGain(start, end, before.evalAt(start) + delta)。
// 基准始终是 before（不取前一变更后的插值），与旧 ToolHandler 拖拽循环逐位一致。
AutomationLane applyVolumeEnvelopeRegionDelta(const AutomationLane& beforeEnvelope,
                                              const std::vector<Note>& notes,
                                              const std::vector<int>& selectedNoteIndices,
                                              float deltaGainDb);

// 单 note 区域归零：setRegionGain(start, end, 0)，与旧双击路径逐位一致。
AutomationLane applyVolumeEnvelopeRegionReset(const AutomationLane& beforeEnvelope,
                                              const std::vector<Note>& notes,
                                              int noteIndex);

// 多选区域 delta 计划。revision 不符 → StaleRevision；indices 空或含越界 → NoTarget；
// after == before → NoChange。
VolumeEnvelopeRegionEditPlan planVolumeEnvelopeRegionDeltaEdit(
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision,
    float deltaGainDb);

// 单 note reset 计划。index 越界 → NoTarget；after == before → NoChange。
VolumeEnvelopeRegionEditPlan planVolumeEnvelopeRegionResetEdit(
    const EditableContentSnapshot& snapshot,
    int noteIndex,
    uint64_t expectedNotesRevision);

} // namespace OpenTune
