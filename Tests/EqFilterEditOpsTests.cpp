// Tests/EqFilterEditOpsTests.cpp
//
// EqFilterEditOps 纯领域函数测试 — 无 JUCE 依赖。

#include "Utils/EqFilterEditOps.h"
#include <iostream>

namespace {

using namespace OpenTune;

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

// 双击插入阈值分类：raw<200→LowShelf, >10000→HighShelf, 否则 Peak
void testInsertTypeClassification()
{
    check(makeEqFilter(199.999, 0.0).type == EqFilterType::LowShelf, "<200 -> LowShelf");
    check(makeEqFilter(200.0, 0.0).type == EqFilterType::Peak, "200 -> Peak");
    check(makeEqFilter(10000.0, 0.0).type == EqFilterType::Peak, "10000 -> Peak");
    check(makeEqFilter(10000.001, 0.0).type == EqFilterType::HighShelf, ">10000 -> HighShelf");
    check(makeEqFilter(0.0, 0.0).type == EqFilterType::LowShelf, "0 -> LowShelf");
    check(makeEqFilter(999999.0, 0.0).type == EqFilterType::HighShelf, "huge -> HighShelf");
}

// 插入默认值与夹取
void testInsertDefaultsAndClamp()
{
    auto f = makeEqFilter(1000.0, 0.0);
    check(f.paletteSlot == -1, "insert leaves paletteSlot unset");
    check(f.q == 1.0f, "insert q = 1");
    check(f.frequencyHz == 1000.0f, "insert freq passthrough");

    auto shelf = makeEqFilter(10.0, 99.0);
    check(shelf.type == EqFilterType::LowShelf && shelf.frequencyHz == 20.0f, "shelf freq clamped to 20");
    check(shelf.gainDb == 12.0f, "gain clamped to +12");

    auto lowFreq = makeEqFilter(100.0, -99.0);
    check(lowFreq.type == EqFilterType::LowShelf, "raw freq below 200 stays shelf");
    check(lowFreq.gainDb == -12.0f, "gain clamped to -12");
    auto midPeak = makeEqFilter(300.0, 0.0);
    check(midPeak.type == EqFilterType::Peak && midPeak.frequencyHz == 500.0f, "peak freq clamped to 500");
    auto overPeak = makeEqFilter(10001.0, 0.0);
    check(overPeak.type == EqFilterType::HighShelf && overPeak.frequencyHz == 10001.0f, "shelf upper not clamped below 20000");
    auto shelfMax = makeEqFilter(30000.0, 0.0);
    check(shelfMax.type == EqFilterType::HighShelf && shelfMax.frequencyHz == 20000.0f, "shelf freq clamped to 20000");
}

// 类型切换夹取 + Cut gain 清零
void testSetType()
{
    EqFilter f;
    f.type = EqFilterType::Peak;
    f.frequencyHz = 300.0f;
    f.gainDb = 6.0f;
    setEqFilterType(f, EqFilterType::Peak);
    check(f.frequencyHz == 500.0f, "type switch clamp to peak range 500");

    f.frequencyHz = 15000.0f;
    setEqFilterType(f, EqFilterType::Peak);
    check(f.frequencyHz == 12000.0f, "type switch clamp to peak range 12000");

    f.frequencyHz = 1000.0f;
    setEqFilterType(f, EqFilterType::LowCut);
    check(f.gainDb == 0.0f, "cut gain zeroed");
    check(f.frequencyHz == 1000.0f, "cut freq in 20..20000 kept");

    f.frequencyHz = 5.0f;
    setEqFilterType(f, EqFilterType::LowShelf);
    check(f.frequencyHz == 20.0f, "shelf type switch clamps low");

    f.frequencyHz = 30000.0f;
    setEqFilterType(f, EqFilterType::HighShelf);
    check(f.frequencyHz == 20000.0f, "shelf type switch clamps high");

    EqFilter g;
    g.gainDb = 6.0f;
    g.frequencyHz = 1000.0f;
    setEqFilterType(g, EqFilterType::HighShelf);
    check(g.gainDb == 6.0f, "shelf keeps gain");
}

// frequency/gain 写入夹取（各类型）
void testSetFreqAndGain()
{
    EqFilter peak;
    peak.type = EqFilterType::Peak;
    setEqFilterFrequencyAndGain(peak, 100.0, 0.0);
    check(peak.frequencyHz == 500.0f, "peak freq clamp low");
    setEqFilterFrequencyAndGain(peak, 50000.0, 0.0);
    check(peak.frequencyHz == 12000.0f, "peak freq clamp high");

    EqFilter shelf;
    shelf.type = EqFilterType::HighShelf;
    setEqFilterFrequencyAndGain(shelf, 10.0, 0.0);
    check(shelf.frequencyHz == 20.0f, "shelf freq clamp low");
    setEqFilterFrequencyAndGain(shelf, 999999.0, 0.0);
    check(shelf.frequencyHz == 20000.0f, "shelf freq clamp high");

    EqFilter cut;
    cut.type = EqFilterType::LowCut;
    setEqFilterFrequencyAndGain(cut, 3.0, -30.0);
    check(cut.frequencyHz == 20.0f, "cut freq clamp low");
    check(cut.gainDb == 0.0f, "cut gain remains zero");
    setEqFilterFrequencyAndGain(cut, 1000.0, 30.0);
    check(cut.gainDb == 0.0f, "cut gain remains zero after frequency update");

    setEqFilterFrequencyAndGain(peak, 1000.0, -30.0);
    check(peak.gainDb == -12.0f, "gain clamp -12");
    setEqFilterFrequencyAndGain(peak, 1000.0, 30.0);
    check(peak.gainDb == 12.0f, "gain clamp +12");
}

// Q 步进 clamp [.25, 9]
void testAdjustQ()
{
    EqFilter f;
    f.q = 1.0f;
    adjustEqFilterQ(f, 0.08);
    check(f.q == 1.08f, "q +0.08");

    f.q = 8.99f;
    adjustEqFilterQ(f, 0.08);
    check(f.q == 9.0f, "q clamp max 9");
    adjustEqFilterQ(f, 0.08);
    check(f.q == 9.0f, "q stays at max");

    f.q = 0.26f;
    adjustEqFilterQ(f, -0.08);
    check(f.q == 0.25f, "q clamp min 0.25");
    adjustEqFilterQ(f, -0.08);
    check(f.q == 0.25f, "q stays at min");
}

// bypass / active toggling
void testToggles()
{
    EqFilter f;
    check(!f.bypassed, "default not bypassed");
    toggleEqFilterBypass(f);
    check(f.bypassed, "bypass toggled on");
    toggleEqFilterBypass(f);
    check(!f.bypassed, "bypass toggled off");

    EqSettings s;
    check(s.active, "default active");
    toggleEqActive(s);
    check(!s.active, "active toggled off");
    toggleEqActive(s);
    check(s.active, "active toggled on");
}

// 插入后（palette 分配前）settings 尚不 isValid（slot -1）；分配后合法。
// palette 随机分配属 renderer 职责，此处只验证纯领域契约：insert leaves unset。
void testInsertLeavesPaletteUnassigned()
{
    EqSettings s;
    s.filters.clear();
    s.filters.push_back(makeEqFilter(1000.0, 0.0));
    check(s.filters[0].paletteSlot == -1, "insert leaves slot unset");
    check(!s.isValid(), "unset slot not yet valid");
}

} // namespace

int main()
{
    testInsertTypeClassification();
    testInsertDefaultsAndClamp();
    testSetType();
    testSetFreqAndGain();
    testAdjustQ();
    testToggles();
    testInsertLeavesPaletteUnassigned();

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "EqFilterEditOpsTests: all passed\n";
    return 0;
}
