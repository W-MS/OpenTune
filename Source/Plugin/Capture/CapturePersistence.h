#pragma once


#include <juce_core/juce_core.h>

namespace OpenTune::Capture {

class CaptureSession;

/**
 * Capture session ↔ binary state codec (CAPz format).
 *
 * Layout:
 *   [u32 CAPTURE_MAGIC = 'CAPz' (0x4341507A)]
 *   [i32 version]
 *   [i32 metadata_xml_length]
 *   [UTF-8 metadata XML (ValueTree::toXmlString)]
 *   [for each non-Capturing segment:
 *       [i64 id]
 *       [i64 creationOrder]
 *       [f64 T_start]
 *       [f64 durationSeconds]
 *       [f64 captureSampleRate]
 *       [i32 captureChannels]
 *       [i32 segmentState]
 *       [i64 hostStartSample]    (v12+: authoritative absolute sample position)
 *       [i64 hostSampleCount]    (v12+: authoritative sample count)
 *       [PCM audio]
 *       [i32 originalF0State]
 *       [i32 detectedKeyRoot]
 *       [i32 detectedKeyScale]
 *       [f32 detectedKeyConfidence]
 *       [i32 detectedKeyOrigin]
 *       [pitch curve payload:
 *           [i32 hopSize][f64 sampleRate][f32[] originalF0][f32[] originalEnergy]
 *           [i32 segmentCount][per segment:
 *               [i32 startFrame][i32 endFrame][f32[] f0Data]
 *               [f32[] baseF0Data]   (v14+)
 *               [i32 source][f32 retuneSpeed][f32 pitchDriftScale][f32 vibratoDepth][f32 vibratoRate]
 *           ]
 *           [i32 erasedRangeCount][per range: [i32 startFrame][i32 endFrameExclusive]]
 *       ]
 *   ]
 *   [u32 CAPTURE_END_MAGIC = 'xCAP' (0x78434150)]
 *
 * CaptureSegmentContent is the persisted content owner. ContentRenderService is
 * republished from the restored owner snapshot; it is not persistence state.
 *
 * v13 adds erased F0 ranges to the pitch curve payload. v14 adds each correction
 * segment's baseF0Data. v12/v13 archives remain readable; their baseF0Data stays
 * empty.
 */
class CapturePersistence
{
public:
    static constexpr int kArchiveVersion = 14;

    static juce::MemoryBlock serialize(const CaptureSession& session);
    static bool deserialize(CaptureSession& session, const juce::MemoryBlock& block);
    static bool validate(const juce::MemoryBlock& block);

    /** Header probe only: returns the CAPz archive version, or 0 when the block
     *  is not a CAPz archive. Payload contents are not validated. */
    static int peekArchiveVersion(const juce::MemoryBlock& block);
};

}  // namespace OpenTune::Capture
