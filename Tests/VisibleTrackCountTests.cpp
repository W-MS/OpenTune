#include "Utils/VisibleTrackCount.h"

#include <cstdio>
#include <vector>

using OpenTune::LoadedTrackVisibilityHint;
using OpenTune::MaxTracks;
using OpenTune::kDefaultVisibleTracks;
using OpenTune::resolveLoadedVisibleTrackCount;

namespace {

int failures = 0;

void check(bool ok, const char* message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

LoadedTrackVisibilityHint track(int trackId, bool placements = false, bool mute = false,
                                bool solo = false, float gain = 1.0f)
{
    LoadedTrackVisibilityHint hint;
    hint.trackId = trackId;
    hint.hasPlacements = placements;
    hint.mute = mute;
    hint.solo = solo;
    hint.gain = gain;
    return hint;
}

void testLegacyEmptyProjectKeepsDefaultHeaders()
{
    const int count = resolveLoadedVisibleTrackCount(0, 0, {});
    check(count == kDefaultVisibleTracks, "empty legacy project stays at the default header count");
}

void testLegacyContentExpandsHeaders()
{
    const std::vector<LoadedTrackVisibilityHint> tracks = {
        track(0, true),
        track(1),
        track(2),
        track(3, true),
    };
    const int count = resolveLoadedVisibleTrackCount(0, 0, tracks);
    check(count == 4, "legacy project with a clip on track 4 shows four headers");
}

void testLegacySoloExpandsHeaders()
{
    const std::vector<LoadedTrackVisibilityHint> tracks = {
        track(0),
        track(1),
        track(2, false, false, true),
    };
    const int count = resolveLoadedVisibleTrackCount(0, 0, tracks);
    check(count == 3, "legacy solo on the third track creates that header");
}

void testLegacySingleOccupiedTrackDoesNotDropBelowDefault()
{
    const int count = resolveLoadedVisibleTrackCount(0, 0, { track(0, true) });
    check(count == kDefaultVisibleTracks,
          "legacy content only on the first track keeps the default second header");
}

void testLegacySelectedTrackExpandsHeaders()
{
    const int count = resolveLoadedVisibleTrackCount(0, 3, { track(0) });
    check(count == 4, "legacy selected track is given a header");
}

void testPersistedCountRestoresEmptyTracksAndCanShrink()
{
    const int count = resolveLoadedVisibleTrackCount(1, 0, {});
    check(count == 1, "saved one-track project restores a single header");
}

void testPersistedCountGrowsToCoverOccupiedTracks()
{
    const std::vector<LoadedTrackVisibilityHint> tracks = {
        track(0, true),
        track(5, false, true),
    };
    const int count = resolveLoadedVisibleTrackCount(2, 0, tracks);
    check(count == 6, "saved header count still grows to cover mute or clips beyond it");
}

void testDefaultGainDoesNotCountAsOccupied()
{
    std::vector<LoadedTrackVisibilityHint> tracks;
    for (int trackId = 0; trackId < MaxTracks; ++trackId)
        tracks.push_back(track(trackId));

    const int count = resolveLoadedVisibleTrackCount(0, 0, tracks);
    check(count == kDefaultVisibleTracks, "untouched tracks do not expand the header count");
}

void testCountIsClamped()
{
    const int count = resolveLoadedVisibleTrackCount(99, 0, {});
    check(count == MaxTracks, "header count is clamped to the track limit");
}

} // namespace

int main()
{
    testLegacyEmptyProjectKeepsDefaultHeaders();
    testLegacyContentExpandsHeaders();
    testLegacySoloExpandsHeaders();
    testLegacySingleOccupiedTrackDoesNotDropBelowDefault();
    testLegacySelectedTrackExpandsHeaders();
    testPersistedCountRestoresEmptyTracksAndCanShrink();
    testPersistedCountGrowsToCoverOccupiedTracks();
    testDefaultGainDoesNotCountAsOccupied();
    testCountIsClamped();

    if (failures != 0) {
        std::fprintf(stderr, "%d visible track count checks failed\n", failures);
        return 1;
    }

    std::printf("VisibleTrackCountTests passed\n");
    return 0;
}
