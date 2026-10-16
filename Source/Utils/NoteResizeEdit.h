#pragma once

#include "Note.h"
#include <vector>

namespace OpenTune {

enum class NoteResizeEdge
{
    None,
    Left,
    Right
};

struct NoteResizeEditPlan
{
    std::vector<Note> afterNotes;
    double dirtyStartTime = 0.0;
    double dirtyEndTime = 0.0;
};

// The globally time-sorted sequence allows resize to yield at most one adjacent note.
NoteResizeEditPlan planNoteResizeEdit(const std::vector<Note>& notes,
                                      int noteIndex,
                                      NoteResizeEdge edge,
                                      double clampedTime,
                                      double minDuration);

} // namespace OpenTune
