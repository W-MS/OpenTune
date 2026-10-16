#pragma once

#include "F0Timeline.h"
#include "Note.h"
#include "Content/ContentEditCommands.h"
#include <cstdint>
#include <vector>

namespace OpenTune {

// ============================================================================
// Scissors 拓扑编辑纯计划器：分割（split）与合并（merge）。
//
// 输入 before notes、splitTime、分离线容差、effective F0 数据与 F0Timeline，
// 输出 afterNotes 及 caller 区分 cut/merge 是否发生、split 右段索引所需的最小
// 数据。算法精确复现 ToolHandler 旧实现：
// - split：严格内部切割（startTime < splitTime < endTime），切割段 dirty=true；
// - merge：分离线按序配对（left 取其后第一个未消费的 right），一次消费，
//   不同 pitch 行各自独立合并；merged 保留 left 其余字段，丢弃 right 其余参数；
// - pitch center：effective F0 区间平均（仅正值帧），avg > 0 时更新 pitch 与
//   pitchOffset=0，avg <= 0 保持原 pitch；floor/ceil 帧边界由 F0Timeline 现有
//   函数给出。
// ============================================================================

struct ScissorsSplitPlan
{
    std::vector<Note> afterNotes;
    int cutCount = 0;        // 被切割的 note 数
    int lastRightIndex = -1; // 最后一个被切割 note 的右段在 afterNotes 中的索引
};

struct ScissorsMergePlan
{
    std::vector<Note> afterNotes;
    int mergeCount = 0;
};

// 严格内部切割；splitTime 在任何音符之外时 afterNotes == beforeNotes、cutCount == 0。
ScissorsSplitPlan planScissorsSplit(const std::vector<Note>& beforeNotes,
                                    double splitTime,
                                    const std::vector<float>& effectiveF0,
                                    const F0Timeline& f0tl);

// 分离线合并；tolerance = 分离线像素容差对应的源时间。未命中返回 mergeCount == 0。
ScissorsMergePlan planScissorsMerge(const std::vector<Note>& beforeNotes,
                                    double splitTime,
                                    double tolerance,
                                    const std::vector<float>& effectiveF0,
                                    const F0Timeline& f0tl);

// 区间 [tStart, tEnd) 内有效 F0（>0）帧的算术平均；无有效帧或数据为空返回 0。
float avgEffectiveF0InRange(const F0Timeline& f0tl,
                            const std::vector<float>& effectiveF0,
                            double tStart,
                            double tEnd);

// ============================================================================
// Scissors 提交事务结果（Processor 单方法 applyScissorsNotes 的返回契约）。
// ============================================================================
enum class ScissorsCommitStatus
{
    Applied,
    StaleRevision,
    Failed
};

struct ScissorsCommitResult
{
    ScissorsCommitStatus status = ScissorsCommitStatus::Failed;
    ContentCommitSnapshot contentSnapshot;
};

} // namespace OpenTune
