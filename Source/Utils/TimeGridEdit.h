#pragma once

#include "TimeGrid.h"
#include <memory>
#include <vector>

namespace OpenTune {

std::shared_ptr<const TimeGridSnapshot> planTimeGridDrag(
    const std::shared_ptr<const TimeGridSnapshot>& original,
    uint64_t draggedHandleId,
    double dragStartOutputSeconds,
    double newOutputSeconds,
    uint64_t selectedHandleId,
    const std::vector<uint64_t>& additionalSelectedIds,
    bool snapDisabled,
    bool& validationRejected);

enum class TimeGridInsertStatus {
    Inserted,
    InvalidTime,
    InvalidGrid,
    OutputSpacing,
    SourceSpacing,
    OutsideRange,
    ValidationRejected
};

struct TimeGridInsertResult {
    TimeGridInsertStatus status{TimeGridInsertStatus::InvalidGrid};
    std::shared_ptr<const TimeGridSnapshot> snapshot;
    uint64_t insertedHandleId{0};
};

TimeGridInsertResult planTimeGridInsert(
    const std::shared_ptr<const TimeGridSnapshot>& original,
    double clickedOutputSeconds);

enum class TimeGridDeleteStatus {
    Deleted,
    NotFound,
    Endpoint,
    ValidationRejected
};

struct TimeGridDeleteResult {
    TimeGridDeleteStatus status{TimeGridDeleteStatus::NotFound};
    std::shared_ptr<const TimeGridSnapshot> snapshot;
};

TimeGridDeleteResult planTimeGridDelete(
    const std::shared_ptr<const TimeGridSnapshot>& original,
    uint64_t selectedHandleId);

} // namespace OpenTune
