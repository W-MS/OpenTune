#include "../Source/Utils/HostTransportCoalescer.h"
#include "../Source/Utils/PendingSeekPolicy.h"

#include <cstdio>
#include <vector>

using namespace OpenTune;

static int failures = 0;

static void check(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static bool isCall(const HostTransportCall& call, HostTransportCall::Kind kind, double seconds = 0.0)
{
    if (call.kind != kind)
        return false;
    if (kind == HostTransportCall::Kind::Seek)
        return call.seconds == seconds;
    return true;
}

static void testPendingSeekHoldsUntilHostMatches()
{
    PendingSeekPolicy::Input in;
    in.pendingTimeSeconds = 12.5;
    in.canonicalTimeSeconds = 1.0;
    in.issuedAtSeconds = 10.0;
    in.nowSeconds = 10.2;
    in.pendingEpoch = 4;
    in.currentEpoch = 9;
    in.hostBound = true;
    in.hostTimeAtGestureSeconds = 1.0;

    const auto held = PendingSeekPolicy::evaluate(in);
    check(held.holdPending && !held.clearPending,
          "a playing host that has not reached the seek stays on the pending time");

    in.canonicalTimeSeconds = 12.53;
    const auto matched = PendingSeekPolicy::evaluate(in);
    check(!matched.holdPending && matched.clearPending,
          "host time within tolerance after a new observation clears the pending seek");

    in.canonicalTimeSeconds = 12.5;
    in.currentEpoch = in.pendingEpoch;
    const auto sameEpoch = PendingSeekPolicy::evaluate(in);
    check(sameEpoch.holdPending && !sameEpoch.clearPending,
          "the observation captured before the request does not count as host confirmation");
}

static void testPendingSeekDeadline()
{
    PendingSeekPolicy::Input in;
    in.pendingTimeSeconds = 4.0;
    in.canonicalTimeSeconds = 0.0;
    in.issuedAtSeconds = 1.0;
    in.nowSeconds = 1.0 + PendingSeekPolicy::holdDeadlineSeconds - 0.01;
    in.pendingEpoch = 1;
    in.currentEpoch = 8;
    in.hostBound = true;
    in.hostTimeAtGestureSeconds = 0.0;

    const auto held = PendingSeekPolicy::evaluate(in);
    check(held.holdPending && !held.clearPending, "pending seek holds inside the deadline");

    in.nowSeconds = 1.0 + PendingSeekPolicy::holdDeadlineSeconds;
    in.hostBound = true;
    const auto expired = PendingSeekPolicy::evaluate(in);
    check(expired.holdPending && expired.detachFromHost && !expired.clearPending,
          "an ignored host seek stays on the edit cursor");

    in.pendingTimeSeconds = -1.0;
    const auto idle = PendingSeekPolicy::evaluate(in);
    check(!idle.holdPending && !idle.clearPending, "no pending seek is a no-op");
}

static void testLocalEditCursor()
{
    PendingSeekPolicy::Input in;
    in.pendingTimeSeconds = 8.0;
    in.canonicalTimeSeconds = 1.0;
    in.hostTimeAtGestureSeconds = 1.0;
    in.pendingEpoch = 2;
    in.currentEpoch = 6;
    in.hostBound = false;

    const auto held = PendingSeekPolicy::evaluate(in);
    check(held.holdPending && !held.clearPending,
          "a host that cannot seek keeps the in-plugin edit cursor");

    in.canonicalTimeSeconds = 8.02;
    const auto landed = PendingSeekPolicy::evaluate(in);
    check(landed.clearPending, "the edit cursor follows once the host lands on it");

    in.canonicalTimeSeconds = 3.5;
    in.hostTimeAtGestureSeconds = 1.0;
    const auto moved = PendingSeekPolicy::evaluate(in);
    check(moved.clearPending && !moved.holdPending,
          "moving the DAW playhead releases the local edit cursor");
}

static void testLogicRestartRule()
{
    check(logicCommitRestartsPlayback(true, true), "Logic restarts a committed seek while playing");
    check(!logicCommitRestartsPlayback(true, false), "Logic seeks in place while stopped");
    check(!logicCommitRestartsPlayback(false, true), "other hosts keep a plain seek while playing");
}

static void testCoalescedTimelineCommit()
{
    HostTransportCoalescer coalescer;
    coalescer.previewSeek(1.0);
    coalescer.previewSeek(2.0);
    coalescer.commitSeek(3.25, true);
    coalescer.previewSeek(4.5);

    const auto batch = coalescer.take();
    check(!coalescer.hasPending(), "take clears the coalescer");
    check(coalescer.take().empty(), "a second take is empty");

    const auto calls = planHostTransportCalls(batch);
    check(calls.size() == 3, "Logic commit is stop, seek, start");
    check(isCall(calls[0], HostTransportCall::Kind::Stop), "restart stops first");
    check(isCall(calls[1], HostTransportCall::Kind::Seek, 4.5), "latest seek wins");
    check(isCall(calls[2], HostTransportCall::Kind::Start), "restart resumes playback");
}

static void testPlainSeekAndTransportOverrides()
{
    HostTransportCoalescer preview;
    preview.previewSeek(8.0);
    const auto previewCalls = planHostTransportCalls(preview.take());
    check(previewCalls.size() == 1 && isCall(previewCalls[0], HostTransportCall::Kind::Seek, 8.0),
          "preview is a position request only");

    HostTransportCoalescer stopped;
    stopped.commitSeek(6.0, false);
    const auto stoppedCalls = planHostTransportCalls(stopped.take());
    check(stoppedCalls.size() == 1 && isCall(stoppedCalls[0], HostTransportCall::Kind::Seek, 6.0),
          "commit while stopped does not restart");

    HostTransportCoalescer playFromSeek;
    playFromSeek.commitSeek(2.0, false);
    playFromSeek.requestStart();
    const auto playCalls = planHostTransportCalls(playFromSeek.take());
    check(playCalls.size() == 2
              && isCall(playCalls[0], HostTransportCall::Kind::Seek, 2.0)
              && isCall(playCalls[1], HostTransportCall::Kind::Start),
          "play from a seek position seeks then starts");

    HostTransportCoalescer replaced;
    replaced.commitSeek(5.0, true);
    replaced.requestStart();
    replaced.requestStop();
    const auto stopCalls = planHostTransportCalls(replaced.take());
    check(stopCalls.size() == 2
              && isCall(stopCalls[0], HostTransportCall::Kind::Stop)
              && isCall(stopCalls[1], HostTransportCall::Kind::Seek, 5.0),
          "stop cancels a pending restart and still lands the seek");

    HostTransportCoalescer stopOnly;
    stopOnly.requestStop();
    const auto stopOnlyCalls = planHostTransportCalls(stopOnly.take());
    check(stopOnlyCalls.size() == 1 && isCall(stopOnlyCalls[0], HostTransportCall::Kind::Stop),
          "stop alone is one stop request");

    HostTransportCoalescer resume;
    resume.requestStop();
    resume.requestStart();
    const auto resumeCalls = planHostTransportCalls(resume.take());
    check(resumeCalls.size() == 1 && isCall(resumeCalls[0], HostTransportCall::Kind::Start),
          "the later transport command wins");
}

static void testCycleIsPreserved()
{
    HostTransportCoalescer coalescer;
    coalescer.requestCycle(true);
    coalescer.commitSeek(1.0, false);
    coalescer.requestCycle(false);
    const auto calls = planHostTransportCalls(coalescer.take());
    check(calls.size() == 2, "seek and cycle both survive");
    check(isCall(calls[0], HostTransportCall::Kind::Seek, 1.0), "seek stays ahead of cycle");
    check(calls[1].kind == HostTransportCall::Kind::Cycle && !calls[1].cycleEnabled,
          "the latest cycle value wins");
}

int main()
{
    testPendingSeekHoldsUntilHostMatches();
    testPendingSeekDeadline();
    testLocalEditCursor();
    testLogicRestartRule();
    testCoalescedTimelineCommit();
    testPlainSeekAndTransportOverrides();
    testCycleIsPreserved();

    if (failures == 0)
    {
        std::puts("All host transport tests passed.");
        return 0;
    }

    std::fprintf(stderr, "%d test(s) FAILED.\n", failures);
    return 1;
}
