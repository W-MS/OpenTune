#include "DrawNoteEdit.h"

#include "PitchUtils.h"
#include <algorithm>
#include <cmath>

namespace OpenTune {

namespace {

float calculateEffectivePIP(const EditableContentSnapshot& snapshot, const Note& note)
{
    if (snapshot.pitchCurve == nullptr || note.endTime <= note.startTime)
        return -1.0f;

    const auto& curve = *snapshot.pitchCurve;
    const auto& originalF0 = curve.getOriginalF0();
    const F0Timeline timeline(curve.getHopSize(), curve.getSampleRate(),
                              static_cast<int>(curve.size()));
    if (timeline.isEmpty())
        return -1.0f;

    const auto noteRange = timeline.nonEmptyRangeForTimes(note.startTime, note.endTime);
    if (noteRange.startFrame >= noteRange.endFrameExclusive)
        return -1.0f;

    const float pitchRatio = static_cast<float>(snapshot.pitchShiftSettings.getPitchRatio());
    std::vector<float> voicedF0;
    voicedF0.reserve(static_cast<std::size_t>(noteRange.endFrameExclusive - noteRange.startFrame));
    for (int frame = noteRange.startFrame; frame < noteRange.endFrameExclusive; ++frame) {
        const float effectiveF0 = originalF0[static_cast<std::size_t>(frame)] * pitchRatio;
        if (effectiveF0 > 0.0f)
            voicedF0.push_back(effectiveF0);
    }

    if (voicedF0.empty())
        return -1.0f;

    std::sort(voicedF0.begin(), voicedF0.end());
    return voicedF0[voicedF0.size() / 2];
}

bool makeNotePatches(const std::vector<Note>& beforeNotes,
                     const std::vector<Note>& afterNotes,
                     ContentNoteRangePatch& beforePatch,
                     ContentNoteRangePatch& afterPatch)
{
    auto notesContentEqual = [](const Note& left, const Note& right) {
        return left.endTime == right.endTime
            && left.pitch == right.pitch
            && left.pitchOffset == right.pitchOffset
            && left.retuneSpeed == right.retuneSpeed
            && left.pitchDriftScale == right.pitchDriftScale
            && left.vibratoDepth == right.vibratoDepth
            && left.vibratoRate == right.vibratoRate
            && left.eq == right.eq;
    };

    double dirtyStartTime = 1e30;
    double dirtyEndTime = -1e30;
    std::size_t beforeIndex = 0;
    std::size_t afterIndex = 0;
    while (beforeIndex < beforeNotes.size() || afterIndex < afterNotes.size()) {
        const bool hasBefore = beforeIndex < beforeNotes.size();
        const bool hasAfter = afterIndex < afterNotes.size();
        if (hasBefore && hasAfter
            && beforeNotes[beforeIndex].startTime == afterNotes[afterIndex].startTime) {
            if (!notesContentEqual(beforeNotes[beforeIndex], afterNotes[afterIndex])) {
                dirtyStartTime = std::min(dirtyStartTime, beforeNotes[beforeIndex].startTime);
                dirtyEndTime = std::max({ dirtyEndTime,
                                          beforeNotes[beforeIndex].endTime,
                                          afterNotes[afterIndex].endTime });
            }
            ++beforeIndex;
            ++afterIndex;
        } else if (!hasAfter
                   || (hasBefore && beforeNotes[beforeIndex].startTime < afterNotes[afterIndex].startTime)) {
            dirtyStartTime = std::min(dirtyStartTime, beforeNotes[beforeIndex].startTime);
            dirtyEndTime = std::max(dirtyEndTime, beforeNotes[beforeIndex].endTime);
            ++beforeIndex;
        } else {
            dirtyStartTime = std::min(dirtyStartTime, afterNotes[afterIndex].startTime);
            dirtyEndTime = std::max(dirtyEndTime, afterNotes[afterIndex].endTime);
            ++afterIndex;
        }
    }

    if (dirtyEndTime <= dirtyStartTime)
        return false;

    beforePatch.affectedRange = { dirtyStartTime, dirtyEndTime };
    afterPatch.affectedRange = beforePatch.affectedRange;
    beforePatch.afterNotesInRange = filterNotesToRange(beforeNotes, dirtyStartTime, dirtyEndTime);
    afterPatch.afterNotesInRange = filterNotesToRange(afterNotes, dirtyStartTime, dirtyEndTime);
    return true;
}

} // namespace

DrawNoteEditPlan planDrawNoteEdit(const EditableContentSnapshot& snapshot,
                                  const DrawNoteEditInput& input,
                                  uint64_t expectedNotesRevision)
{
    DrawNoteEditPlan plan;
    if (snapshot.notesRevision != expectedNotesRevision) {
        plan.status = DrawNotePlanStatus::StaleRevision;
        return plan;
    }

    plan.afterNotes = snapshot.notes;
    if (input.pitch <= 0.0f)
        return plan;

    std::vector<Note> updatedNotes;
    updatedNotes.reserve(plan.afterNotes.size() + 2);
    for (const auto& note : plan.afterNotes) {
        const bool overlaps = note.endTime > input.startSeconds
            && note.startTime < input.endSeconds;
        if (!overlaps) {
            updatedNotes.push_back(note);
            continue;
        }

        if (note.startTime < input.startSeconds) {
            Note left = note;
            left.endTime = input.startSeconds;
            if (left.endTime > left.startTime)
                updatedNotes.push_back(left);
        }

        if (note.endTime > input.endSeconds) {
            Note right = note;
            right.startTime = input.endSeconds;
            if (right.endTime > right.startTime)
                updatedNotes.push_back(right);
        }
    }

    Note newNote;
    newNote.startTime = input.startSeconds;
    newNote.endTime = input.endSeconds;
    newNote.pitch = input.pitch;
    newNote.pitchOffset = 0.0f;
    newNote.retuneSpeed = input.retuneSpeed;
    newNote.vibratoDepth = input.vibratoDepth;
    newNote.vibratoRate = input.vibratoRate;
    newNote.dirty = true;

    const float newPip = calculateEffectivePIP(snapshot, newNote);
    if (newPip > 0.0f) {
        const float sourceMidi = PitchUtils::freqToMidi(newPip);
        newNote.pitch = PitchUtils::midiToFreq(std::round(sourceMidi));
        newNote.originalPitch = newPip;

        const float targetMidi = PitchUtils::freqToMidi(input.pitch);
        newNote.pitchOffset = targetMidi - std::round(sourceMidi);
    } else {
        newNote.pitch = input.pitch;
        newNote.originalPitch = input.pitch;
        newNote.pitchOffset = 0.0f;
    }

    plan.insertedNote = newNote;
    NoteSequence finalSequence;
    finalSequence.setNotesSorted(updatedNotes);
    finalSequence.insertNoteSorted(newNote);
    plan.afterNotes = finalSequence.getNotes();
    if (!makeNotePatches(snapshot.notes, plan.afterNotes, plan.beforePatch, plan.afterPatch)) {
        plan.status = DrawNotePlanStatus::NoChange;
        return plan;
    }

    if (snapshot.pitchCurve != nullptr) {
        const auto& curveSnapshot = snapshot.pitchCurve;
        const F0Timeline timeline(curveSnapshot->getHopSize(), curveSnapshot->getSampleRate(),
                                  static_cast<int>(curveSnapshot->size()));
        const F0FrameRange noteRange = timeline.rangeForTimes(input.startSeconds, input.endSeconds);
        if (!noteRange.isEmpty()) {
            plan.pitchCurveEdited = true;
            plan.affectedRange = PitchCurve::expandNoteBasedCorrectionRange(
                noteRange.startFrame, noteRange.endFrameExclusive, timeline.endFrameExclusive());

            auto correctedCurve = PitchCurve::fromSnapshot(curveSnapshot);
            const auto correctedSnapshot = correctedCurve->applyCorrectionToRange(
                plan.afterNotes,
                noteRange.startFrame,
                noteRange.endFrameExclusive,
                static_cast<float>(snapshot.pitchShiftSettings.getPitchRatio()),
                input.retuneSpeed,
                input.vibratoDepth,
                input.vibratoRate);

            for (const auto& segment : correctedSnapshot->getCorrectionSegments()) {
                if (segment.startFrame < plan.affectedRange.endFrameExclusive
                    && segment.endFrame > plan.affectedRange.startFrame)
                    plan.segmentsToCommit.push_back(segment);
            }

            const double rangeStartSeconds = timeline.timeAtFrame(plan.affectedRange.startFrame);
            const double rangeEndSeconds = timeline.timeAtFrame(plan.affectedRange.endFrameExclusive);
            plan.beforeNotesInPitchRange = filterNotesToRange(
                snapshot.notes, rangeStartSeconds, rangeEndSeconds);
            plan.beforeSegments = clipSegmentsToFrameRange(
                curveSnapshot->getCorrectionSegments(),
                plan.affectedRange.startFrame,
                plan.affectedRange.endFrameExclusive);
        }
    }

    plan.status = DrawNotePlanStatus::Ready;
    return plan;
}

} // namespace OpenTune
