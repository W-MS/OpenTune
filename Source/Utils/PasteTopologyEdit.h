#pragma once

#include "Content/ContentEditCommands.h"
#include <cstdint>
#include <vector>

namespace OpenTune {

enum class PasteNotesStatus
{
    Ready,
    Applied,
    EmptyClipboard,
    StaleRevision,
    Failed
};

// 粘贴事务计划：clipboard notes（source 时间）+ 显式 paste start（source seconds）
// → 平移后的 pastedNotes（dirty=true，时长与相对间距保持）+ planPasteTopology 拓扑计划。
struct PasteNotesEditPlan
{
    PasteNotesStatus status = PasteNotesStatus::EmptyClipboard;
    std::vector<Note> pastedNotes;
    ContentNoteRangePatch beforePatch;
    ContentNoteRangePatch afterPatch;
};

// 纯计算：revision 校验（不匹配 → StaleRevision）→ 剪贴板平移到 pasteStartTime
// （保留各字段与时长，dirty=true）→ planPasteTopology 生成 before/after 拓扑补丁。
PasteNotesEditPlan planPasteNotesEdit(
    const EditableContentSnapshot& snapshot,
    const std::vector<Note>& clipboardNotes,
    double pasteStartTime,
    uint64_t expectedNotesRevision);

} // namespace OpenTune
