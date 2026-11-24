#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace metamorfo::ui
{

// Visual identity (SPEC §8): "retro-futuristic mutation lab". Dark military olive
// powder-coated panel, bone-white condensed technical lettering, cream cylindrical
// knobs with black caps, phosphor green / cyan LEDs, red only for locks.
namespace palette
{
inline const juce::Colour panelTop { 0xff474e31 };
inline const juce::Colour panelBottom { 0xff2c3220 };
inline const juce::Colour panelEdge { 0xff1b1f13 };
inline const juce::Colour bone { 0xffe8e0c8 };
inline const juce::Colour boneDim { 0xffa9a28c };
inline const juce::Colour phosphor { 0xff7dff8e };
inline const juce::Colour cyan { 0xff5ff2e0 };
inline const juce::Colour lockRed { 0xffff4a3d };
inline const juce::Colour cream { 0xffeadfc3 };
inline const juce::Colour creamShade { 0xffb9ad8e };
inline const juce::Colour cap { 0xff151515 };
inline const juce::Colour glass { 0xff0d1a11 };
inline const juce::Colour engraving { 0xff1e2316 };
} // namespace palette

class MetamorfoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MetamorfoLookAndFeel();

    // Fonts embedded from resources (SIL OFL).
    [[nodiscard]] juce::Font labelFont (float height) const;   // Barlow Condensed SemiBold
    [[nodiscard]] juce::Font textFont (float height) const;    // Barlow Condensed Medium
    [[nodiscard]] juce::Font displayFont (float height) const; // Share Tech Mono

    juce::Font getPopupMenuFont() override { return textFont (16.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    // Shared drawing helpers.
    static void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
    static void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, bool on);
    static void drawEngravedText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                                  const juce::Font& font, juce::Justification just, juce::Colour colour = palette::bone);

private:
    juce::Typeface::Ptr label_, text_, display_;
};

} // namespace metamorfo::ui
