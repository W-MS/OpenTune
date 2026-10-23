// CaptureSession span-budget exhaustion and scan-coverage lifecycle tests.
// Drives the real CaptureSession through arm → processBlock → tick, using the
// test-only small span capacity to exercise the full exhaustion lifecycle.

#include "../Source/Plugin/Capture/CaptureSession.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using OpenTune::ContentKey;
using OpenTune::DomainKind;
using OpenTune::Capture::CaptureSession;
using OpenTune::Capture::CaptureSegment;
using OpenTune::Capture::ProcessorBindings;
using OpenTune::Capture::SegmentState;

struct TestBindings
{
    int publishCount = 0;

    ProcessorBindings make()
    {
        ProcessorBindings b;
        b.publishPlaybackSource =
            [this](const ContentKey&,
                   const std::shared_ptr<const OpenTune::EditableContentSnapshot>&,
                   const std::shared_ptr<const juce::AudioBuffer<float>>&,
                   double) {
                ++publishCount;
            };
        return b;
    }
};

// Feed one silent-but-nonzero block through the audio path.
void feedBlock(CaptureSession& session,
               int numSamples,
               int64_t hostSample,
               bool playing,
               double sampleRate = 48000.0)
{
    juce::AudioBuffer<float> buffer(2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < numSamples; ++s)
            buffer.setSample(ch, s, 0.25f);
    session.processBlock(buffer, hostSample, sampleRate, playing);
}

int validateSpanTreeNode(const CaptureSegment& segment,
                         int index,
                         int parent,
                         bool hasLower,
                         int64_t lower,
                         bool hasUpper,
                         int64_t upper,
                         std::vector<unsigned char>& visited,
                         bool& valid)
{
    if (index < 0)
        return 0;
    if (index >= static_cast<int>(visited.size()) || visited[static_cast<size_t>(index)] != 0) {
        valid = false;
        return 0;
    }

    visited[static_cast<size_t>(index)] = 1;
    const auto& span = segment.spans[static_cast<size_t>(index)];
    if (span.treeParent != parent
        || (hasLower && span.hostStartSample <= lower)
        || (hasUpper && span.hostStartSample >= upper))
        valid = false;

    const int leftHeight = validateSpanTreeNode(segment, span.treeLeft, index,
                                                hasLower, lower, true, span.hostStartSample,
                                                visited, valid);
    const int rightHeight = validateSpanTreeNode(segment, span.treeRight, index,
                                                 true, span.hostStartSample, hasUpper, upper,
                                                 visited, valid);
    const int height = 1 + std::max(leftHeight, rightHeight);
    const int balance = leftHeight - rightHeight;
    if (balance > 1 || balance < -1 || span.treeHeight != height)
        valid = false;
    return height;
}

bool validateSpanTree(const CaptureSegment& segment)
{
    const int count = segment.numSpans.load(std::memory_order_acquire);
    std::vector<unsigned char> visited(static_cast<size_t>(count), 0);
    bool valid = true;
    validateSpanTreeNode(segment, segment.spanTreeRoot, -1,
                         false, std::numeric_limits<int64_t>::lowest(),
                         false, std::numeric_limits<int64_t>::max(), visited, valid);
    for (const auto seen : visited)
        valid = valid && seen != 0;
    return valid;
}

bool testSpanBudgetExhaustionDropsWholeTake()
{
    TestBindings bindings;
    CaptureSession session(bindings.make());
    session.prepareToPlay(48000.0, 512, 2);

    // Tiny span capacity: 2 spans per take. Discontinuities come from gaps in
    // host sample positions (simulated by jumping the host position between blocks).
    session.testSetSpanCapacityOverride(2);

    if (!session.armNewCapture())
        return false;

    // Block 1: normal write (span 0).
    feedBlock(session, 512, 0, true);
    // Block 2: position jump → discontinuity → span 1.
    feedBlock(session, 512, 100000, true);
    // Block 3: another jump → span budget (2) exhausted mid-take.
    feedBlock(session, 512, 200000, true);

    CaptureSegment* capturing = nullptr;
    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) == SegmentState::Capturing)
            capturing = seg.get();
    }
    if (capturing == nullptr)
        return false;
    if (!capturing->spanBudgetExhausted.load(std::memory_order_acquire))
        return false;
    const int writtenAtExhaustion = capturing->fifo.getTotalWrittenSamples();
    feedBlock(session, 512, 200512, true);
    if (capturing->fifo.getTotalWrittenSamples() != writtenAtExhaustion)
        return false;

    // User stops: an exhausted take must not enter Pending / publish partial data.
    session.stopCapture();

    bool sawPending = false;
    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) == SegmentState::Pending)
            sawPending = true;
    }
    if (sawPending)
        return false;

    // Model the race where stopCapture changed the state to Pending just before
    // the audio thread published its exhaustion latch.
    capturing->state.store(SegmentState::Pending, std::memory_order_release);

    // Tick observes the latch, waits for writerActive=false, reclaims the take.
    for (int i = 0; i < 8; ++i)
        session.tick();

    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) == SegmentState::Capturing
            || seg->state.load(std::memory_order_acquire) == SegmentState::Pending)
            return false;
    }
    if (bindings.publishCount != 0)
        return false;  // no partial success result was published

    // Session is idle again; a new arm is possible.
    return session.armNewCapture();
}

bool testProcessingRangeDoesNotTriggerNewArm()
{
    TestBindings bindings;
    CaptureSession session(bindings.make());
    session.prepareToPlay(48000.0, 512, 2);

    // A Processing segment covers [1s, 3s) at 48 kHz.
    session.testInjectProcessingSegment(1.0, 2.0, 1, nullptr, 48000.0);

    // Simulate playback parked inside the Processing range.
    session.setScanArmed(true);
    session.updateTransportScan();  // lastHostSample_ is -1 → not covered yet

    // Arm must have happened (position not yet known), then stop it.
    session.stopCapture();

    // Push a host position inside the Processing segment and re-evaluate.
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    session.processBlock(buffer, 48000 * 2, 48000.0, true);  // 2 s: inside Processing
    session.updateTransportScan();

    // Playback over covered (Processing) audio must not start a new capture.
    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) == SegmentState::Capturing)
            return false;
    }
    return true;
}

bool testBlockCrossingProcessingRangeRecordsOnlyOutsideParts()
{
    TestBindings bindings;
    CaptureSession session(bindings.make());
    session.prepareToPlay(48000.0, 2048, 2);

    // Processing segment covering the first three quarters of the upcoming block:
    // block [0, 2048), covered [0, 1536).
    session.testInjectProcessingSegment(0.0, 1536.0 / 48000.0, 1, nullptr, 48000.0);

    if (!session.armNewCapture())
        return false;

    feedBlock(session, 2048, 0, true);

    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) != SegmentState::Capturing)
            continue;
        if (seg->numSpans.load(std::memory_order_acquire) != 1)
            return false;
        const auto& after = seg->spans[0];
        if (after.hostStartSample != 1536 || after.sampleCount != 512)
            return false;
        const int written = seg->fifo.getTotalWrittenSamples();
        return written == 512;
    }
    return false;
}

bool testMoreThan64ProcessingRangesThroughCaptureSession()
{
    TestBindings bindings;
    CaptureSession session(bindings.make());
    session.prepareToPlay(1.0, 160, 2);

    for (int i = 0; i < 80; ++i) {
        const int start = i * 2;
        session.testInjectProcessingSegment(
            static_cast<double>(start),
            1.0,
            static_cast<uint64_t>(i + 100),
            nullptr,
            1.0);
    }
    if (!session.armNewCapture())
        return false;

    feedBlock(session, 160, 0, true, 1.0);
    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) != SegmentState::Capturing)
            continue;
        const int spanCount = seg->numSpans.load(std::memory_order_acquire);
        const int written = seg->fifo.getTotalWrittenSamples();
        if (spanCount != 80 || written != 80) {
            std::fprintf(stderr, "CaptureSession ranges: spans=%d written=%d\n", spanCount, written);
            return false;
        }
        for (int i = 0; i < 80; ++i) {
            const auto& span = seg->spans[static_cast<size_t>(i)];
            if (span.hostStartSample != i * 2 + 1 || span.sampleCount != 1) {
                std::fprintf(stderr, "CaptureSession range[%d]: start=%lld count=%d\n",
                             i, static_cast<long long>(span.hostStartSample), span.sampleCount);
                return false;
            }
        }

        // Forward playback extends the final span and indexed lookup skips the rest.
        feedBlock(session, 160, 160, true, 1.0);
        if (seg->numSpans.load(std::memory_order_acquire) != 80 || !validateSpanTree(*seg))
            return false;

        // A backward seek still recognizes all previously captured coverage.
        const int writtenBeforeSeek = seg->fifo.getTotalWrittenSamples();
        feedBlock(session, 160, 0, true, 1.0);
        if (seg->fifo.getTotalWrittenSamples() != writtenBeforeSeek)
            return false;

        // Resuming forward playback also finds the extended final span.
        feedBlock(session, 160, 160, true, 1.0);
        if (seg->numSpans.load(std::memory_order_acquire) != 80 || !validateSpanTree(*seg)
            || seg->fifo.getTotalWrittenSamples() != writtenBeforeSeek)
            return false;
        return true;
    }
    std::fputs("CaptureSession ranges: no active take found\n", stderr);
    return false;
}

bool testOutOfOrderSpanIndexInsertion()
{
    TestBindings bindings;
    CaptureSession session(bindings.make());
    session.prepareToPlay(1.0, 1, 2);
    if (!session.armNewCapture())
        return false;

    for (int i = 79; i >= 0; --i)
        feedBlock(session, 1, i * 2, true, 1.0);

    CaptureSegment* capturing = nullptr;
    for (auto& seg : session.testSegments()) {
        if (seg->state.load(std::memory_order_acquire) == SegmentState::Capturing)
            capturing = seg.get();
    }
    if (capturing == nullptr || capturing->numSpans.load(std::memory_order_acquire) != 80
        || !validateSpanTree(*capturing))
        return false;

    // Query from the beginning after reverse-order insertion. Existing even
    // samples stay covered while the odd gaps become the only new capture.
    feedBlock(session, 160, 0, true, 1.0);
    return validateSpanTree(*capturing)
        && capturing->fifo.getTotalWrittenSamples() == 160;
}

bool testReclaimWaitsForPublishedViewReaders()
{
    std::mutex gateMutex;
    std::condition_variable gate;
    bool callbackEntered = false;
    bool releaseCallback = false;
    std::atomic<int> retiredCount { 0 };

    ProcessorBindings bindings;
    bindings.replaceWithRendered = [&](juce::AudioBuffer<float>&, int, int, ContentKey, int64_t, double) {
        std::unique_lock<std::mutex> lock(gateMutex);
        if (callbackEntered)
            return;
        callbackEntered = true;
        gate.notify_all();
        gate.wait(lock, [&] { return releaseCallback; });
    };
    bindings.retireSegment = [&](ContentKey) {
        retiredCount.fetch_add(1, std::memory_order_relaxed);
    };

    CaptureSession session(std::move(bindings));
    session.prepareToPlay(48000.0, 512, 2);
    session.testInjectEditedSegment(0.0, 1.0, 1, nullptr, 48000.0);
    session.testInjectEditedSegment(0.0, 1.0, 2, nullptr, 48000.0);
    const uint64_t coveringId = session.testInjectProcessingSegment(0.0, 1.0, 3, nullptr, 48000.0);

    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        session.processBlock(buffer, 0, 48000.0, true);
    });

    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        entered = gate.wait_for(lock, std::chrono::seconds(2), [&] { return callbackEntered; });
    }
    if (entered) {
        session.onRenderComplete(ContentKey{DomainKind::RegularVST3Capture, coveringId, 0});
        for (int i = 0; i < 8; ++i)
            session.tick();
    }
    const bool heldWhileReaderActive = retiredCount.load(std::memory_order_relaxed) == 0;

    {
        std::lock_guard<std::mutex> lock(gateMutex);
        releaseCallback = true;
    }
    gate.notify_all();
    audioThread.join();

    for (int i = 0; i < 8; ++i)
        session.tick();
    return entered && heldWhileReaderActive
        && retiredCount.load(std::memory_order_relaxed) == 2;
}

bool testEditedStillReplacesPlaybackWhileProcessingDoesNot()
{
    TestBindings bindings;
    CaptureSession session(bindings.make());
    session.prepareToPlay(48000.0, 512, 2);

    int replaceCalls = 0;
    ProcessorBindings b = bindings.make();
    b.replaceWithRendered = [&](juce::AudioBuffer<float>&, int, int, ContentKey, int64_t, double) {
        ++replaceCalls;
    };
    CaptureSession replaceSession(std::move(b));
    replaceSession.prepareToPlay(48000.0, 512, 2);

    // Edited segment → replacement playback still happens.
    replaceSession.testInjectEditedSegment(0.0, 1.0, 1, nullptr, 48000.0);
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    replaceSession.processBlock(buffer, 0, 48000.0, true);
    if (replaceCalls == 0)
        return false;

    // Processing segment → no replacement.
    replaceSession.testInjectProcessingSegment(2.0, 1.0, 2, nullptr, 48000.0);
    const int before = replaceCalls;
    replaceSession.processBlock(buffer, 48000 * 2, 48000.0, true);
    return replaceCalls == before;
}

} // namespace

int main()
{
    int failed = 0;
    if (!testSpanBudgetExhaustionDropsWholeTake()) {
        std::fputs("FAIL: span budget exhaustion drops the whole take\n", stderr);
        ++failed;
    }
    if (!testProcessingRangeDoesNotTriggerNewArm()) {
        std::fputs("FAIL: Processing range does not trigger a new arm\n", stderr);
        ++failed;
    }
    if (!testBlockCrossingProcessingRangeRecordsOnlyOutsideParts()) {
        std::fputs("FAIL: block crossing Processing range records only outside parts\n", stderr);
        ++failed;
    }
    if (!testMoreThan64ProcessingRangesThroughCaptureSession()) {
        std::fputs("FAIL: CaptureSession handles more than 64 Processing ranges\n", stderr);
        ++failed;
    }
    if (!testOutOfOrderSpanIndexInsertion()) {
        std::fputs("FAIL: span index handles out-of-order insertion\n", stderr);
        ++failed;
    }
    if (!testReclaimWaitsForPublishedViewReaders()) {
        std::fputs("FAIL: reclaim waits for published-view readers\n", stderr);
        ++failed;
    }
    if (!testEditedStillReplacesPlaybackWhileProcessingDoesNot()) {
        std::fputs("FAIL: Edited replaces playback, Processing does not\n", stderr);
        ++failed;
    }
    if (failed == 0) {
        std::puts("All CaptureSession span-budget and scan-coverage tests passed.");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED.\n", failed);
    return 1;
}
