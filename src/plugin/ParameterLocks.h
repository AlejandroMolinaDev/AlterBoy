#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace metamorfo
{

// Per-parameter locks (SPEC §6): a locked parameter keeps its value when a preset
// (or the other A/B slot) is loaded. Stored as a child of the APVTS state, so the
// locks travel with the project / FL preset automatically. Message thread only.
class ParameterLocks
{
public:
    static constexpr const char* kNodeType = "Locks";

    explicit ParameterLocks (juce::AudioProcessorValueTreeState& state) : state_ (state) {}

    [[nodiscard]] bool isLocked (const juce::String& paramId) const
    {
        const auto node = state_.state.getChildWithName (kNodeType);
        return node.isValid() && static_cast<bool> (node.getProperty (paramId, false));
    }

    void setLocked (const juce::String& paramId, bool locked)
    {
        // Only real parameter IDs: anything else would be stored as an XML attribute
        // name in the plug-in state (an invalid name, e.g. with a space, makes the whole
        // saved state unparsable).
        if (state_.getParameter (paramId) == nullptr)
            return;
        auto node = state_.state.getOrCreateChildWithName (kNodeType, nullptr);
        if (locked)
            node.setProperty (paramId, true, nullptr);
        else
            node.removeProperty (paramId, nullptr);
    }

    void toggle (const juce::String& paramId) { setLocked (paramId, ! isLocked (paramId)); }

private:
    juce::AudioProcessorValueTreeState& state_;
};

} // namespace metamorfo
