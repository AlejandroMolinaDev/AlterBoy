#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace metamorfo::ui
{

class MetamorfoLookAndFeel;

// Three-position aviation-style toggle lever for MODE (SPEC §8: not buttons), with
// engraved labels and one LED per position. Click a label / LED or drag the lever.
class ToggleLever3 final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleLever3 (juce::RangedAudioParameter& param, juce::StringArray labels, MetamorfoLookAndFeel& lnf);
    ~ToggleLever3() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    [[nodiscard]] int position() const noexcept { return position_; }

private:
    [[nodiscard]] juce::Point<float> pivot() const;
    [[nodiscard]] float labelY (int index) const;
    [[nodiscard]] int indexForY (float y) const;
    void select (int index);

    juce::RangedAudioParameter& param_;
    juce::StringArray labels_;
    MetamorfoLookAndFeel& lnf_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    int position_ = 0;
    bool dragging_ = false;
};

} // namespace metamorfo::ui
