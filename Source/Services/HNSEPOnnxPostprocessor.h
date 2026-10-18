#pragma once

#include "HnsepAudio.h"

#include <memory>
#include <string>
#include <vector>

namespace Ort { struct Env; }

namespace OpenTune {

struct HNSEPOutput
{
    std::shared_ptr<const std::vector<float>> harmonic;
    std::shared_ptr<const std::vector<float>> residual;
};

// Loads <modelsDir>/hnsep.onnx using the process-shared ORT Env. The model
// contract is mono 44.1kHz waveform [1,1,N] -> float waveform_out [1,1,N].
bool runHNSEPOnnx(const float* dryAudio,
                  size_t sampleCount,
                  const std::string& modelsDir,
                  Ort::Env& env,
                  HNSEPOutput& output,
                  std::string& errorMessage);

} // namespace OpenTune
