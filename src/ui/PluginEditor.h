#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace metamorfo
{
class MetamorfoProcessor;
}

namespace metamorfo::ui
{

// Phase 1 diagnostic editor: shows the last MIDI note received, the bus layout,
// the plugin format and the host. Final design arrives in phase 6 (SPEC §8).
// Reads processor state only through atomics; closing it never affects audio.
class MetamorfoEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit MetamorfoEditor (MetamorfoProcessor& owner);
    ~MetamorfoEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshStaticInfo();

    MetamorfoProcessor& processor_;

    juce::Label title_;
    juce::Label noteValue_;
    juce::Label noteCaption_;
    juce::Label details_;
    juce::Label info_;
    juce::Label pitch_;

    int shownNote_ = -2;
    int shownHeld_ = -2;
    std::uint32_t shownCount_ = 0xffffffffu;
    int ticks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetamorfoEditor)
};

} // namespace metamorfo::ui
