#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PresetManager.h"

namespace metamorfo::ui
{

class MetamorfoLookAndFeel;

// Top bar (SPEC §6/§8): previous / next, preset menu by category, save, A/B
// comparison, UI scale (75-200 %) and options (MIDI help, MIDI reference note,
// portamento, presets folder).
class PresetBar final : public juce::Component
{
public:
    PresetBar (PresetManager& presets, juce::AudioProcessorValueTreeState& state, MetamorfoLookAndFeel& lnf);

    std::function<void (float)> onScaleChosen;
    std::function<float()> currentScale;
    std::function<void()> onShowMidiHelp;

    void refresh(); // preset name / A-B state (called by the editor timer)
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showPresetMenu();
    void showScaleMenu();
    void showOptionsMenu();
    void promptSave();

    PresetManager& presets_;
    juce::AudioProcessorValueTreeState& state_;
    MetamorfoLookAndFeel& lnf_;

    juce::TextButton previous_ { juce::String::fromUTF8 ("◀") }, next_ { juce::String::fromUTF8 ("▶") };
    juce::TextButton name_, save_ { "GUARDAR" }, ab_ { "A" }, copy_ { juce::String::fromUTF8 ("A→B") };
    juce::TextButton scale_ { "150 %" }, options_ { juce::String::fromUTF8 ("OPCIONES") };
    juce::String shownName_;
    int shownSlot_ = -1;
};

// Full-panel help overlay "Control por MIDI en mi DAW" (FL Studio first).
class MidiHelpScreen final : public juce::Component
{
public:
    explicit MidiHelpScreen (MetamorfoLookAndFeel& lnf);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    MetamorfoLookAndFeel& lnf_;
    juce::TextButton close_ { juce::String::fromUTF8 ("CERRAR ✕") };
    juce::TextEditor text_;
};

} // namespace metamorfo::ui
