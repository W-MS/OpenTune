#pragma once

/**
 * EQ Filter 编辑纯领域函数 — 阈值分类 / 夹取 / 切换
 *
 * 只处理 EqFilter 数值与领域数据，不做 palette 分配（随机分配留在
 * EqGraphRenderer::assignPaletteSlots）。插入时 paletteSlot 置 -1，
 * 由调用方经 renderer 分配后再产出合法 EqSettings。
 */

#include "../Utils/NoteEqSettings.h"

namespace OpenTune {

/** 按双击插入阈值分类并夹取 freq/gain：raw<200→LowShelf, >10000→HighShelf, 否则 Peak；paletteSlot=-1, q=1。 */
EqFilter makeEqFilter(double freqHz, double gainDb);

/** 切换类型：频率按新类型夹取（Peak [500,12000]，其余 [20,20000]），Cut 类型 gain 归零。 */
void setEqFilterType(EqFilter& filter, EqFilterType newType);

/** 按当前类型夹取并写入 frequency/gain（拖拽与旋钮共用）。 */
void setEqFilterFrequencyAndGain(EqFilter& filter, double freqHz, double gainDb);

/** Q 步进调节，clamp 到 [kMinQ, kMaxQ]。delta 由调用方（UI 滚轮）传入。 */
void adjustEqFilterQ(EqFilter& filter, double delta);

/** 单段 bypass 切换。 */
void toggleEqFilterBypass(EqFilter& filter);

/** 全局 active 切换。 */
void toggleEqActive(EqSettings& settings);

} // namespace OpenTune
