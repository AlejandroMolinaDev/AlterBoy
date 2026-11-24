#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MetamorfoLookAndFeel.h"
#include "ui/components/Knob.h"
#include "ui/components/PresetBar.h"
#include "ui/components/ToggleLever3.h"

namespace metamorfo
{
class MetamorfoProcessor;
}

namespace metamorfo::ui
{

// Final editor (SPEC §8): one horizontal row, ~3:1, everything visible.
//   top bar: presets, A/B, UI scale, options
//   left:    PITCH and FORMANT knobs with LINK (LED + connecting line) between them
//   centre:  logo, 3-position MODE lever with LEDs, note display (in -> out)
//   right:   DRIVE and MIX knobs
// All content is laid out at a base size of 780 x 260 and scaled (75-200 %) with a
// transform, so it stays crisp on HiDPI. Reads audio state only through atomics.
class MetamorfoEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseWidth = 780;
    static constexpr int kBaseHeight = 260;

    explicit MetamorfoEditor (MetamorfoProcessor& owner);
    ~MetamorfoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // UI scale persisted between sessions (user settings file).
    static float loadSavedScale();
    static void saveScale (float scale);

private:
    class Content;

    void timerCallback() override;
    void applyScale (float scale);
    [[nodiscard]] float currentScale() const;

    MetamorfoLookAndFeel lnf_;
    std::unique_ptr<Content> content_;
    juce::TooltipWindow tooltips_ { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetamorfoEditor)
};

} // namespace metamorfo::ui
