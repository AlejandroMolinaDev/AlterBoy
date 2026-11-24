#pragma once

#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ParameterLocks.h"

namespace metamorfo::ui
{

class MetamorfoLookAndFeel;

// Original cream cylindrical knob with black cap and black indicator line (SPEC §8),
// with the utilities of SPEC §6:
//  * double-click: default value      * mouse wheel: adjust
//  * Shift (Win) / Cmd (Mac) drag: fine adjustment
//  * click on a scale mark: jump to that value
//  * click on the label: toggle name / exact value
//  * right-click: "Escribir valor", "Bloquear / Desbloquear", "Valor por defecto"
//  * locked parameters show their label in alert red
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Scale
    {
        std::vector<float> majorTicks;           // plain values with a number
        std::vector<juce::String> majorLabels;   // same size as majorTicks
        float minorStep = 0.0f;                  // 0 = none
    };

    Knob (juce::AudioProcessorValueTreeState& state, ParameterLocks& locks, const juce::String& paramId,
          const juce::String& caption, Scale scale, MetamorfoLookAndFeel& lnf);
    ~Knob() override;

    // When set (e.g. FORMANT with Link on), the knob shows this plain value instead of its
    // own and ignores the mouse (it mirrors another parameter).
    void setMirror (std::function<float()> mirrorValue, const juce::String& mirrorCaption);
    void clearMirror();

    void refresh(); // called by the editor timer (lock colour, mirrored value)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    class KnobSlider;

    [[nodiscard]] juce::Point<float> centre() const;
    [[nodiscard]] float radius() const;
    [[nodiscard]] float angleFor (float plainValue) const;
    [[nodiscard]] juce::Rectangle<float> captionArea() const;
    void showContextMenu();
    void promptForValue();

    juce::AudioProcessorValueTreeState& state_;
    ParameterLocks& locks_;
    juce::String paramId_, caption_;
    Scale scale_;
    MetamorfoLookAndFeel& lnf_;
    juce::RangedAudioParameter* param_ = nullptr;

    std::unique_ptr<KnobSlider> slider_;
    std::unique_ptr<juce::SliderParameterAttachment> attachment_;

    std::function<float()> mirror_;
    juce::String mirrorCaption_;
    bool showValue_ = false;
    bool locked_ = false;

    static constexpr float kStartAngle = juce::MathConstants<float>::pi * -0.75f;
    static constexpr float kEndAngle = juce::MathConstants<float>::pi * 0.75f;
};

} // namespace metamorfo::ui
