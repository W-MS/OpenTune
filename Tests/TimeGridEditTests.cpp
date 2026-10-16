#include "Utils/TimeGridEdit.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace OpenTune;

TimeHandle handle(uint64_t id, double source, double output, HandleKind kind = HandleKind::UserAdded)
{
    TimeHandle result;
    result.id = id;
    result.source_seconds = source;
    result.output_seconds = output;
    result.kind = kind;
    return result;
}

std::shared_ptr<const TimeGridSnapshot> grid(std::vector<TimeHandle> handles)
{
    return TimeGridSnapshot::makeFromHandles(std::move(handles));
}

std::shared_ptr<const TimeGridSnapshot> dragGrid()
{
    return grid({
        handle(10, 0.0, 0.0, HandleKind::ClipStart),
        handle(11, 0.4, 0.4),
        handle(12, 0.8, 0.8),
        handle(13, 1.0, 0.84),
        handle(14, 1.2, 1.2, HandleKind::ClipEnd)
    });
}

bool checkSingleHandleClamp()
{
    const auto original = dragGrid();
    bool validationRejected = false;
    const auto result = planTimeGridDrag(
        original, 11, 0.4, 1.0, 11, {}, false, validationRejected);
    return result != nullptr
        && !validationRejected
        && result->handles()[1].output_seconds == 0.77
        && result->handles()[2].output_seconds == 0.8;
}

bool checkSelectedGroupTwoPassClamp()
{
    const auto original = dragGrid();
    bool validationRejected = false;
    const auto result = planTimeGridDrag(
        original, 11, 0.4, 1.0, 11, { 13 }, false, validationRejected);
    if (result == nullptr || validationRejected)
        return false;
    return result->handles()[1].output_seconds == 0.77
        && result->handles()[2].output_seconds == 0.8
        && result->handles()[3].output_seconds == 1.17
        && result->handles()[4].output_seconds == 1.2;
}

bool checkTightGroupRegionAborts()
{
    const auto original = grid({
        handle(20, 0.0, 0.0, HandleKind::ClipStart),
        handle(21, 0.2, 0.2),
        handle(22, 0.4, 0.21),
        handle(23, 0.6, 0.22),
        handle(24, 1.0, 0.6),
        handle(25, 1.2, 1.2, HandleKind::ClipEnd)
    });
    if (original == nullptr)
        return false;
    bool validationRejected = false;
    const auto result = planTimeGridDrag(
        original, 21, 0.2, 0.5, 21, { 22, 23 }, false, validationRejected);
    return result == nullptr && !validationRejected;
}

std::shared_ptr<const TimeGridSnapshot> insertGrid()
{
    return grid({
        handle(31, 0.0, 0.0, HandleKind::ClipStart),
        handle(47, 0.4, 0.3),
        handle(35, 0.8, 0.8),
        handle(39, 1.2, 1.2, HandleKind::ClipEnd)
    });
}

bool checkInsertMappingIdAndKind()
{
    const auto result = planTimeGridInsert(insertGrid(), 0.55);
    if (result.status != TimeGridInsertStatus::Inserted || result.snapshot == nullptr
        || result.insertedHandleId != 48 || result.snapshot->handles().size() != 5)
        return false;
    const auto& inserted = result.snapshot->handles()[2];
    return inserted.id == 48
        && inserted.kind == HandleKind::UserAdded
        && std::abs(inserted.source_seconds - 0.6) < 1e-12
        && inserted.output_seconds == 0.55;
}

bool checkInsertOutputAndSourceGapRejections()
{
    const auto outputGap = planTimeGridInsert(insertGrid(), 0.32);
    const auto sourceGap = planTimeGridInsert(insertGrid(), 0.4);
    return outputGap.status == TimeGridInsertStatus::OutputSpacing
        && outputGap.snapshot == nullptr
        && sourceGap.status == TimeGridInsertStatus::SourceSpacing
        && sourceGap.snapshot == nullptr;
}

bool checkInsertBoundariesRejected()
{
    const auto atStart = planTimeGridInsert(insertGrid(), 0.0);
    const auto atExistingHandle = planTimeGridInsert(insertGrid(), 0.3);
    const auto beyondEnd = planTimeGridInsert(insertGrid(), 1.25);
    return atStart.status == TimeGridInsertStatus::InvalidTime
        && atExistingHandle.status == TimeGridInsertStatus::OutputSpacing
        && beyondEnd.status == TimeGridInsertStatus::SourceSpacing;
}

bool checkDeleteSelectedHandleAndEndpoints()
{
    const auto original = insertGrid();
    const auto deleted = planTimeGridDelete(original, 47);
    const auto clipStart = planTimeGridDelete(original, 31);
    const auto clipEnd = planTimeGridDelete(original, 39);
    const auto missing = planTimeGridDelete(original, 99);
    return deleted.status == TimeGridDeleteStatus::Deleted
        && deleted.snapshot != nullptr
        && deleted.snapshot->handles().size() == 3
        && deleted.snapshot->handles()[1].id == 35
        && clipStart.status == TimeGridDeleteStatus::Endpoint
        && clipStart.snapshot == nullptr
        && clipEnd.status == TimeGridDeleteStatus::Endpoint
        && clipEnd.snapshot == nullptr
        && missing.status == TimeGridDeleteStatus::NotFound
        && missing.snapshot == nullptr;
}

} // namespace

int main()
{
    if (!checkSingleHandleClamp()) {
        std::cerr << "TimeGrid single-handle clamp failed\n";
        return 1;
    }
    if (!checkSelectedGroupTwoPassClamp()) {
        std::cerr << "TimeGrid selected-group two-pass clamp failed\n";
        return 1;
    }
    if (!checkTightGroupRegionAborts()) {
        std::cerr << "TimeGrid tight group region abort failed\n";
        return 1;
    }
    if (!checkInsertMappingIdAndKind()) {
        std::cerr << "TimeGrid insert mapping/id/kind failed\n";
        return 1;
    }
    if (!checkInsertOutputAndSourceGapRejections()) {
        std::cerr << "TimeGrid insert spacing rejection failed\n";
        return 1;
    }
    if (!checkInsertBoundariesRejected()) {
        std::cerr << "TimeGrid insert boundary rejection failed\n";
        return 1;
    }
    if (!checkDeleteSelectedHandleAndEndpoints()) {
        std::cerr << "TimeGrid selected-handle delete failed\n";
        return 1;
    }
    return 0;
}
