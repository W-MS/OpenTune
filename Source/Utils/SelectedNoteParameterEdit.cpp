#include "SelectedNoteParameterEdit.h"
#include "Content/EditableContentSnapshot.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace OpenTune {
namespace {

float clampParameter(AudioEditingScheme::ParameterId id, float value)
{
    switch (id) {
        case AudioEditingScheme::ParameterId::RetuneSpeed:
            return std::clamp(value, 0.0f, 1.0f);
        case AudioEditingScheme::ParameterId::VibratoDepth:
            return std::clamp(value, 0.0f, 100.0f);
        case AudioEditingScheme::ParameterId::VibratoRate:
            return std::clamp(value, 3.0f, 12.0f);
        case AudioEditingScheme::ParameterId::NoteSplit:
            break;
    }
    return value;
}

void setNoteParameter(Note& note, AudioEditingScheme::ParameterId id, float value)
{
    switch (id) {
        case AudioEditingScheme::ParameterId::RetuneSpeed: note.retuneSpeed = value; break;
        case AudioEditingScheme::ParameterId::VibratoDepth: note.vibratoDepth = value; break;
        case AudioEditingScheme::ParameterId::VibratoRate: note.vibratoRate = value; break;
        case AudioEditingScheme::ParameterId::NoteSplit: break;
    }
}

float getNoteParameter(const Note& note, AudioEditingScheme::ParameterId id)
{
    switch (id) {
        case AudioEditingScheme::ParameterId::RetuneSpeed: return note.retuneSpeed;
        case AudioEditingScheme::ParameterId::VibratoDepth: return note.vibratoDepth;
        case AudioEditingScheme::ParameterId::VibratoRate: return note.vibratoRate;
        case AudioEditingScheme::ParameterId::NoteSplit: return 0.0f;
    }
    return 0.0f;
}

std::vector<Note> notesInRange(const std::vector<Note>& notes, double startSeconds, double endSeconds)
{
    std::vector<Note> result;
    for (const auto& note : notes) {
        if (note.startTime < endSeconds && note.endTime > startSeconds)
            result.push_back(note);
    }
    return result;
}

std::vector<PitchCorrectionSegment> segmentsInRange(
    const std::vector<PitchCorrectionSegment>& segments, int startFrame, int endFrame)
{
    std::vector<PitchCorrectionSegment> result;
    for (const auto& segment : segments) {
        if (segment.endFrame <= startFrame || segment.startFrame >= endFrame)
            continue;

        const int clippedStart = std::max(segment.startFrame, startFrame);
        const int clippedEnd = std::min(segment.endFrame, endFrame);
        const int offset = clippedStart - segment.startFrame;
        const int length = clippedEnd - clippedStart;
        if (offset < 0 || length <= 0 || offset + length > static_cast<int>(segment.f0Data.size()))
            continue;

        auto clipped = segment;
        clipped.startFrame = clippedStart;
        clipped.endFrame = clippedEnd;
        clipped.f0Data.assign(segment.f0Data.begin() + offset,
                              segment.f0Data.begin() + offset + length);
        result.push_back(std::move(clipped));
    }
    return result;
}

} // namespace

std::optional<SelectedNoteParameterPatch> planSelectedNoteParameterEdit(
    ContentKey contentKey,
    const EditableContentSnapshot& snapshot,
    const std::vector<int>& selectedNoteIndices,
    uint64_t expectedNotesRevision,
    AudioEditingScheme::ParameterId parameterId,
    float value,
    float retuneSpeedDefault,
    float vibratoDepthDefault,
    float vibratoRateDefault)
{
    if (!contentKey.isValid() || snapshot.notesRevision != expectedNotesRevision
        || snapshot.pitchCurve == nullptr
        || parameterId == AudioEditingScheme::ParameterId::NoteSplit)
        return std::nullopt;

    const auto curveSnapshot = snapshot.pitchCurve;
    const F0Timeline timeline(curveSnapshot->getHopSize(), curveSnapshot->getSampleRate(),
                              static_cast<int>(curveSnapshot->size()));
    if (timeline.isEmpty())
        return std::nullopt;

    SelectedNoteParameterPatch patch;
    patch.contentKey = contentKey;
    auto afterNotes = snapshot.notes;
    const float clampedValue = clampParameter(parameterId, value);
    double dirtyStartTime = 1e30;
    double dirtyEndTime = -1e30;
    bool anySelected = false;

    for (const int noteIndex : selectedNoteIndices) {
        if (noteIndex < 0 || noteIndex >= static_cast<int>(afterNotes.size()))
            continue;

        auto& note = afterNotes[static_cast<size_t>(noteIndex)];
        anySelected = true;
        if (std::abs(clampedValue - getNoteParameter(note, parameterId)) > 1e-6f)
            patch.changed = true;
        setNoteParameter(note, parameterId, clampedValue);
        note.dirty = true;
        dirtyStartTime = std::min(dirtyStartTime, note.startTime);
        dirtyEndTime = std::max(dirtyEndTime, note.endTime);
    }

    if (!anySelected)
        return std::nullopt;
    if (!patch.changed)
        return patch;

    const auto editRange = timeline.rangeForTimes(dirtyStartTime, dirtyEndTime);
    if (editRange.isEmpty())
        return patch;

    auto derivedCurve = PitchCurve::fromSnapshot(curveSnapshot);
    const auto derivedSnapshot = derivedCurve->applyCorrectionToRange(
        afterNotes,
        editRange.startFrame,
        editRange.endFrameExclusive,
        static_cast<float>(snapshot.pitchShiftSettings.getPitchRatio()),
        retuneSpeedDefault,
        vibratoDepthDefault,
        vibratoRateDefault);

    patch.affectedRange = PitchCurve::expandNoteBasedCorrectionRange(
        editRange.startFrame, editRange.endFrameExclusive, timeline.endFrameExclusive());
    const double affectedStartSeconds = timeline.timeAtFrame(patch.affectedRange.startFrame);
    const double affectedEndSeconds = timeline.timeAtFrame(patch.affectedRange.endFrameExclusive);
    patch.beforeNotes = notesInRange(snapshot.notes, affectedStartSeconds, affectedEndSeconds);
    patch.afterNotes = notesInRange(afterNotes, affectedStartSeconds, affectedEndSeconds);
    patch.beforeSegments = segmentsInRange(
        curveSnapshot->getCorrectionSegments(),
        patch.affectedRange.startFrame,
        patch.affectedRange.endFrameExclusive);
    patch.afterSegments = segmentsInRange(
        derivedSnapshot->getCorrectionSegments(),
        patch.affectedRange.startFrame,
        patch.affectedRange.endFrameExclusive);
    return patch;
}

} // namespace OpenTune
