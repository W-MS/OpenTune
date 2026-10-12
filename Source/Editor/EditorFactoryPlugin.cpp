// Host-plugin TU (attached to AU/VST3 targets); a wrong-target attachment fails fast.
#include "EditorFactory.h"
#include "Plugin/PluginEditor.h"
#include "PluginProcessor.h"

namespace OpenTune {

juce::AudioProcessorEditor* createOpenTuneEditor(OpenTuneAudioProcessor& processor)
{
    return new PluginUI::OpenTuneAudioProcessorEditor(processor);
}

} // namespace OpenTune
