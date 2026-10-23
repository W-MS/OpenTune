#include "../Source/Utils/ExternalEditLaunch.h"
#include "../Source/Utils/MagnifyZoom.h"
#include "../Source/Utils/WaveformTime.h"
#include "../Source/Utils/WheelPan.h"
#include "../Source/Plugin/Capture/CaptureScanPlan.h"

#include <cstdio>

using namespace OpenTune;

static int failures = 0;

static void check(bool ok, const char* message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void testWheelPan()
{
    const auto smooth = wheelEventToPixels(0.5f / 256.0f, -1.0f / 256.0f, true, 90.0f);
    check(smooth.x > 0.9 && smooth.x < 1.1, "a smooth trackpad delta maps back to points");
    check(smooth.y < -1.9 && smooth.y > -2.1, "vertical trackpad motion is kept when horizontal is also present");

    const auto notch = wheelEventToPixels(0.0f, 1.0f, false, 90.0f);
    check(notch.x == 0.0 && notch.y == 90.0, "a notched wheel uses the scroll speed setting");
}

static void testScanPlan()
{
    using namespace OpenTune::Capture;
    HostSampleRange covered[] = { { 100, 50 }, { 0, 0 }, { 180, 40 } };
    BlockSampleRun runs[4] = {};
    int produced = 0;
    const int count = forEachUncoveredRun(
        80, 160,
        [&](int64_t pos) {
            CoverageProbeResult r;
            r.nextStart = 240;
            for (const auto& range : covered) {
                const int64_t end = range.start + range.count;
                if (pos >= range.start && pos < end) r.insideEnd = r.insideEnd > end ? r.insideEnd : end;
                else if (range.start > pos && range.start < r.nextStart) r.nextStart = range.start;
            }
            return r;
        },
        [&](int offset, int samples) {
            runs[produced++] = { offset, samples };
            return true;
        });
    check(count == 3 && produced == 3, "a block keeps every gap around scanned ranges");
    check(runs[0].blockOffset == 0 && runs[0].sampleCount == 20, "audio before a scanned range is captured");
    check(runs[1].blockOffset == 70 && runs[1].sampleCount == 30, "the gap between scanned ranges is captured");
    check(runs[2].blockOffset == 140 && runs[2].sampleCount == 20, "audio after the last scanned range is captured");
    {
        // One block crossing a single long covered range: only the outside parts.
        int runsSeen = 0;
        int firstOffset = -1, firstCount = -1, secondOffset = -1, secondCount = -1;
        forEachUncoveredRun(
            1000, 200,
            [](int64_t pos) {
                const int64_t covStart = 1050, covEnd = 1150;
                if (pos >= covStart && pos < covEnd)
                    return CoverageProbeResult{ covEnd, 1000 + 200 };
                return CoverageProbeResult{ pos, covStart };
            },
            [&](int offset, int samples) {
                if (runsSeen == 0) { firstOffset = offset; firstCount = samples; }
                else if (runsSeen == 1) { secondOffset = offset; secondCount = samples; }
                ++runsSeen;
                return true;
            });
        check(runsSeen == 2, "a block crossing a covered range writes only the outside parts");
        check(firstOffset == 0 && firstCount == 50, "audio before the covered range is captured");
        check(secondOffset == 150 && secondCount == 50, "audio after the covered range is captured");
    }
    {
        // More than 64 covered ranges in one block: every gap, no duplicates.
        HostSampleRange many[80];
        for (int i = 0; i < 80; ++i)
            many[i] = { i * 10, 5 };  // covered [i*10, i*10+5), gap [i*10+5, (i+1)*10)
        int runsSeen = 0;
        int64_t lastEnd = -1;
        bool ordered = true;
        forEachUncoveredRun(
            0, 800,
            [&](int64_t pos) {
                CoverageProbeResult r;
                r.nextStart = 800;
                for (int i = 0; i < 80; ++i) {
                    const int64_t s = many[i].start;
                    const int64_t e = s + many[i].count;
                    if (pos >= s && pos < e) r.insideEnd = r.insideEnd > e ? r.insideEnd : e;
                    else if (s > pos && s < r.nextStart) r.nextStart = s;
                }
                return r;
            },
            [&](int offset, int samples) {
                if (offset <= lastEnd) ordered = false;
                lastEnd = offset + samples;
                ++runsSeen;
                return true;
            });
        check(runsSeen == 80, "more than 64 covered ranges still produce every gap");
        check(ordered, "streamed runs are ordered and non-overlapping");
    }

    ScanTransportInput armed;
    armed.scanArmed = true;
    armed.hostPlaying = true;
    check(decideScanTransport(armed) == ScanTransportAction::Arm, "playback of new audio starts a scan");

    armed.positionCovered = true;
    check(decideScanTransport(armed) == ScanTransportAction::Hold, "already scanned audio is not captured again");

    ScanTransportInput capturing;
    capturing.scanArmed = true;
    capturing.hostPlaying = false;
    capturing.capturing = true;
    check(decideScanTransport(capturing) == ScanTransportAction::Stop, "stopping the DAW ends the current take");

    ScanTransportInput disarmed;
    disarmed.hostPlaying = true;
    check(decideScanTransport(disarmed) == ScanTransportAction::Hold, "a disarmed scan stays idle");
}

static void testMagnifyZoom()
{
    const auto idle = magnifyScaleToZoom(1.0f);
    check(!idle.apply, "a pinch with no scale change does not zoom");

    const auto enlarge = magnifyScaleToZoom(1.02f);
    check(enlarge.apply && enlarge.factor > 1.019 && enlarge.factor < 1.021,
          "a pinch-out step enlarges by that factor");

    const auto shrink = magnifyScaleToZoom(0.9f);
    check(shrink.apply && shrink.factor > 0.899 && shrink.factor < 0.901,
          "a pinch-in step shrinks by that factor");

    check(!magnifyScaleToZoom(0.0f).apply, "a zero scale is ignored");
    check(!magnifyScaleToZoom(-0.2f).apply, "a negative scale is ignored");

    const auto spike = magnifyScaleToZoom(20.0f);
    check(spike.apply && spike.factor == 4.0, "one pinch event cannot enlarge more than four times");
}

static void testWaveformTime()
{
    constexpr int samplesPerPeak = 32;
    const int64_t atStart = waveformPeakIndexAtSourceTime(0.0, samplesPerPeak, 48000.0);
    const int64_t at48k = waveformPeakIndexAtSourceTime(1.0, samplesPerPeak, 48000.0);
    const int64_t at44k = waveformPeakIndexAtSourceTime(1.0, samplesPerPeak, 44100.0);
    check(atStart == 0, "waveform and notes share the same origin");
    check(at48k == 1500, "one second at 48 kHz addresses the real sample bucket");
    check(at44k == 1378, "a fixed 44100 Hz clock addresses an earlier bucket");
    check(at48k > at44k, "using 44100 for 48 kHz audio leaves the waveform behind the notes");
}

static void testExternalEditLaunch()
{
    const char* args[] = { "OpenTune", "--external-edit", "/tmp/session" };
    const auto launch = parseExternalEditLaunch(3, args);
    check(launch.active && launch.sessionDirectory == "/tmp/session", "the external edit flag carries the session directory");

    const char* inlineArgs[] = { "OpenTune", "--external-edit=/tmp/other" };
    const auto inlineLaunch = parseExternalEditLaunch(2, inlineArgs);
    check(inlineLaunch.active && inlineLaunch.sessionDirectory == "/tmp/other", "an equals form is accepted");

    const char* plain[] = { "OpenTune" };
    check(!parseExternalEditLaunch(1, plain).active, "a normal launch is not an external edit");
}

int main()
{
    testWheelPan();
    testMagnifyZoom();
    testWaveformTime();
    testScanPlan();
    testExternalEditLaunch();
    if (failures == 0) {
        std::puts("All view and capture plan tests passed.");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED.\n", failures);
    return 1;
}
