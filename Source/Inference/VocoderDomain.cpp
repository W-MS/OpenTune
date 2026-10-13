#include "VocoderDomain.h"
#include "VocoderInferenceService.h"
#include "VocoderRenderScheduler.h"
#include "../Utils/AppLogger.h"

namespace OpenTune {

VocoderDomain::VocoderDomain(std::shared_ptr<Ort::Env> env)
    : inferenceService_(std::make_unique<VocoderInferenceService>(std::move(env))) {}

VocoderDomain::~VocoderDomain() {
    shutdown();
}

bool VocoderDomain::initialize(const std::string& modelPath, std::string* errorMessage) {
    if (!inferenceService_->initialize(modelPath, errorMessage)) {
        AppLogger::error("[VocoderDomain] Failed to initialize inference service");
        return false;
    }

    scheduler_ = std::make_unique<VocoderRenderScheduler>();
    std::string schedulerError;
    if (!scheduler_->initialize(inferenceService_.get(), &schedulerError)) {
        if (errorMessage != nullptr)
            *errorMessage = schedulerError;
        AppLogger::error("[VocoderDomain] Failed to initialize scheduler");
        scheduler_.reset();
        inferenceService_->shutdown();
        return false;
    }

    AppLogger::info("[VocoderDomain] Initialized successfully");
    return true;
}

void VocoderDomain::shutdown() {
    // Scheduler owns its worker join in its destructor.  Resetting it is the
    // single shutdown path; calling shutdown() first would run the same
    // idempotent protocol twice.
    scheduler_.reset();
    if (inferenceService_) {
        inferenceService_->shutdown();
    }
}

bool VocoderDomain::submit(Job job, std::string* rejectionReason) {
    if (!scheduler_) {
        if (rejectionReason != nullptr)
            *rejectionReason = "Vocoder domain has no scheduler";
        return false;
    }

    VocoderRenderScheduler::Job schedulerJob;
    schedulerJob.f0 = std::move(job.f0);
    schedulerJob.uv = std::move(job.uv);
    schedulerJob.conditioning = std::move(job.conditioning);
    schedulerJob.onComplete = std::move(job.onComplete);
    return scheduler_->submit(std::move(schedulerJob), rejectionReason);
}

int VocoderDomain::getVocoderHopSize() const {
    return inferenceService_ ? inferenceService_->getVocoderHopSize() : 0;
}

int VocoderDomain::getConditioningBins() const {
    return inferenceService_ ? inferenceService_->getConditioningBins() : 0;
}

VocoderConditioningType VocoderDomain::getConditioningType() const {
    return inferenceService_ ? inferenceService_->getConditioningType()
                             : VocoderConditioningType::LogMel;
}

float VocoderDomain::getFMax() const {
    return inferenceService_ ? inferenceService_->getFMax() : 16000.0f;
}

MelFilterbankSpec VocoderDomain::getMelFilterbankSpec() const {
    return inferenceService_ ? inferenceService_->getMelFilterbankSpec() : MelFilterbankSpec{};
}

float VocoderDomain::getMelLogEps() const {
    return inferenceService_ ? inferenceService_->getMelLogEps() : 1.0e-5f;
}

} // namespace OpenTune
