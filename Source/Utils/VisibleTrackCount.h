#pragma once

#include "TrackConstants.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace OpenTune {

// 一条轨道在工程快照里对“是否需要显示轨道头”有贡献的字段。
struct LoadedTrackVisibilityHint {
    int trackId = 0;
    bool hasPlacements = false;
    bool mute = false;
    bool solo = false;
    float gain = 1.0f;
};

// 决定加载后应显示多少条轨道头。
// persistedVisibleTrackCount > 0 时以保存值为准，并至少覆盖仍有内容或混音状态的轨道。
// 旧工程没有该字段时，按最高占用轨道上扩，且不少于默认两条。
inline int resolveLoadedVisibleTrackCount(int persistedVisibleTrackCount,
                                          int selectedTrackId,
                                          const std::vector<LoadedTrackVisibilityHint>& tracks)
{
    int highestOccupied = -1;
    for (const auto& track : tracks) {
        if (track.trackId < 0 || track.trackId >= MaxTracks)
            continue;

        const bool occupied = track.hasPlacements
            || track.mute
            || track.solo
            || std::abs(track.gain - 1.0f) > 1.0e-4f;
        if (occupied)
            highestOccupied = std::max(highestOccupied, track.trackId);
    }

    int minimumFromContent = highestOccupied < 0 ? 0 : highestOccupied + 1;
    if (selectedTrackId > 0 && selectedTrackId < MaxTracks)
        minimumFromContent = std::max(minimumFromContent, selectedTrackId + 1);

    const auto clampCount = [](int count) {
        return std::max(1, std::min(MaxTracks, count));
    };

    if (persistedVisibleTrackCount > 0)
        return clampCount(std::max(persistedVisibleTrackCount, minimumFromContent));

    if (minimumFromContent > 0)
        return clampCount(std::max(kDefaultVisibleTracks, minimumFromContent));

    return kDefaultVisibleTracks;
}

} // namespace OpenTune
