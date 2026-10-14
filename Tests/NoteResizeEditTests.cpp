#include "Utils/NoteResizeEdit.h"
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace OpenTune;

Note note(double start, double end, bool dirty = false)
{
    Note result;
    result.startTime = start;
    result.endTime = end;
    result.dirty = dirty;
    return result;
}

bool near(double left, double right)
{
    return std::abs(left - right) < 0.000001;
}

bool checkResizeWithinGaps()
{
    const std::vector<Note> notes = {
        note(0.0, 1.0), note(2.0, 4.0), note(5.0, 6.0)
    };
    const auto left = planNoteResizeEdit(notes, 1, NoteResizeEdge::Left, 1.5, 0.02);
    if (!near(left.afterNotes[1].startTime, 1.5)
        || left.afterNotes[0].endTime != 1.0
        || !left.afterNotes[1].dirty
        || !near(left.dirtyStartTime, 1.5)
        || left.dirtyEndTime != 4.0)
        return false;

    const auto right = planNoteResizeEdit(notes, 1, NoteResizeEdge::Right, 4.5, 0.02);
    return near(right.afterNotes[1].endTime, 4.5)
        && right.afterNotes[2].startTime == 5.0
        && !right.afterNotes[2].dirty
        && right.dirtyStartTime == 2.0
        && near(right.dirtyEndTime, 4.5);
}

bool checkNeighborYieldAndDirtyRanges()
{
    const auto left = planNoteResizeEdit(
        { note(1.0, 2.0), note(3.0, 5.0) }, 1, NoteResizeEdge::Left, 0.0, 0.02);
    if (!near(left.afterNotes[0].endTime, 1.02)
        || !left.afterNotes[0].dirty
        || !near(left.afterNotes[1].startTime, 1.02)
        || !near(left.dirtyStartTime, 1.0)
        || left.dirtyEndTime != 5.0)
        return false;

    const auto right = planNoteResizeEdit(
        { note(1.0, 3.0), note(4.0, 5.0) }, 0, NoteResizeEdge::Right, 6.0, 0.02);
    return near(right.afterNotes[0].endTime, 4.98)
        && near(right.afterNotes[1].startTime, 4.98)
        && right.afterNotes[1].dirty
        && right.dirtyStartTime == 1.0
        && right.dirtyEndTime == 5.0;
}

bool checkTargetMinimumDuration()
{
    const auto left = planNoteResizeEdit(
        { note(2.0, 2.1) }, 0, NoteResizeEdge::Left, 9.0, 0.02);
    const auto right = planNoteResizeEdit(
        { note(2.0, 2.1) }, 0, NoteResizeEdge::Right, -1.0, 0.02);
    return near(left.afterNotes[0].startTime, 2.08)
        && near(left.afterNotes[0].endTime - left.afterNotes[0].startTime, 0.02)
        && near(right.afterNotes[0].endTime, 2.02)
        && near(right.afterNotes[0].endTime - right.afterNotes[0].startTime, 0.02);
}

bool checkShortNeighborsKeepTheirEndpoints()
{
    const auto left = planNoteResizeEdit(
        { note(1.0, 1.01), note(2.0, 4.0) }, 1, NoteResizeEdge::Left, 0.0, 0.02);
    if (left.afterNotes[0].startTime != 1.0
        || left.afterNotes[0].endTime != 1.01
        || left.afterNotes[0].dirty
        || !near(left.afterNotes[1].startTime, 1.01))
        return false;

    const auto right = planNoteResizeEdit(
        { note(1.0, 3.0), note(4.0, 4.01) }, 0, NoteResizeEdge::Right, 6.0, 0.02);
    return right.afterNotes[1].startTime == 4.0
        && right.afterNotes[1].endTime == 4.01
        && !right.afterNotes[1].dirty
        && right.afterNotes[0].endTime == 4.0;
}

} // namespace

int main()
{
    if (!checkResizeWithinGaps()) {
        std::cerr << "Note resize gap boundaries failed\n";
        return 1;
    }
    if (!checkNeighborYieldAndDirtyRanges()) {
        std::cerr << "Note resize neighbor yield/dirty range failed\n";
        return 1;
    }
    if (!checkTargetMinimumDuration()) {
        std::cerr << "Note resize target minimum duration failed\n";
        return 1;
    }
    if (!checkShortNeighborsKeepTheirEndpoints()) {
        std::cerr << "Note resize short-neighbor boundary failed\n";
        return 1;
    }
    return 0;
}
