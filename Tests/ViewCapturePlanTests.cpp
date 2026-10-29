#include "../Source/Utils/ExternalEditLaunch.h"
#include "../Source/Utils/MagnifyZoom.h"
#include "../Source/Utils/WaveformTime.h"
#include "../Source/Utils/WheelPan.h"

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
    testExternalEditLaunch();
    if (failures == 0) {
        std::puts("All view and capture plan tests passed.");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED.\n", failures);
    return 1;
}
