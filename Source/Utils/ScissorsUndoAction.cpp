#include "ScissorsUndoAction.h"

namespace OpenTune {

ScissorsUndoAction::ScissorsUndoAction(std::shared_ptr<ContentEditCommands> commands,
                                       ContentKey key,
                                       juce::String description,
                                       std::vector<Note> beforeNotes,
                                       std::vector<Note> afterNotes)
    : commands_(std::move(commands))
    , contentKey_(key)
    , description_(std::move(description))
    , beforeNotes_(std::move(beforeNotes))
    , afterNotes_(std::move(afterNotes))
{
}

void ScissorsUndoAction::applyNotes(const std::vector<Note>& notes)
{
    if (!commands_->replaceContentNotesForFullMutation(contentKey_, notes))
        return;
    commands_->republishPlaybackSource(contentKey_);
}

void ScissorsUndoAction::undo()
{
    applyNotes(beforeNotes_);
}

void ScissorsUndoAction::redo()
{
    applyNotes(afterNotes_);
}

} // namespace OpenTune
