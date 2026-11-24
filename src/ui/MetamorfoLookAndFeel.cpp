#include "ui/MetamorfoLookAndFeel.h"

#include <MetamorfoBinary.h>

namespace metamorfo::ui
{

MetamorfoLookAndFeel::MetamorfoLookAndFeel()
{
    label_ = juce::Typeface::createSystemTypefaceFor (MetamorfoBinary::BarlowCondensedSemiBold_ttf,
                                                      MetamorfoBinary::BarlowCondensedSemiBold_ttfSize);
    text_ = juce::Typeface::createSystemTypefaceFor (MetamorfoBinary::BarlowCondensedMedium_ttf,
                                                     MetamorfoBinary::BarlowCondensedMedium_ttfSize);
    display_ = juce::Typeface::createSystemTypefaceFor (MetamorfoBinary::ShareTechMonoRegular_ttf,
                                                        MetamorfoBinary::ShareTechMonoRegular_ttfSize);

    using namespace palette;
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff20251a));
    setColour (juce::PopupMenu::textColourId, bone);
    setColour (juce::PopupMenu::headerTextColourId, boneDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3d4a2a));
    setColour (juce::PopupMenu::highlightedTextColourId, phosphor);
    setColour (juce::TextButton::textColourOffId, bone);
    setColour (juce::TextButton::textColourOnId, phosphor);
    setColour (juce::TooltipWindow::textColourId, bone);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff262b1d));
    setColour (juce::AlertWindow::textColourId, bone);
    setColour (juce::AlertWindow::outlineColourId, panelEdge);
    setColour (juce::TextEditor::backgroundColourId, glass);
    setColour (juce::TextEditor::textColourId, phosphor);
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff2f5a3a));
    setColour (juce::TextEditor::outlineColourId, panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, cyan.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, phosphor);
}

juce::Font MetamorfoLookAndFeel::labelFont (float height) const { return juce::Font (juce::FontOptions (label_).withHeight (height)); }
juce::Font MetamorfoLookAndFeel::textFont (float height) const { return juce::Font (juce::FontOptions (text_).withHeight (height)); }
juce::Font MetamorfoLookAndFeel::displayFont (float height) const { return juce::Font (juce::FontOptions (display_).withHeight (height)); }

juce::Font MetamorfoLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return labelFont (std::min (17.0f, static_cast<float> (buttonHeight) * 0.68f));
}

void MetamorfoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                 bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 3.0f;
    juce::ColourGradient grad (juce::Colour (0xff2a3020), r.getX(), r.getY(),
                               juce::Colour (0xff1c2015), r.getX(), r.getBottom(), false);
    if (down)
        grad = juce::ColourGradient (juce::Colour (0xff171a11), r.getX(), r.getY(), juce::Colour (0xff22281a), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, corner);
    g.setColour (highlighted ? palette::boneDim.withAlpha (0.55f) : palette::panelEdge);
    g.drawRoundedRectangle (r, corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine (static_cast<int> (r.getY()) + 1, r.getX() + 2.0f, r.getRight() - 2.0f);
}

void MetamorfoLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (juce::Colour (0xf01a1e14));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (palette::boneDim.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    juce::TextLayout layout;
    juce::AttributedString s;
    s.append (text, textFont (15.0f), palette::bone);
    s.setJustification (juce::Justification::centredLeft);
    layout.createLayout (s, static_cast<float> (width) - 16.0f);
    layout.draw (g, r.reduced (8.0f, 4.0f));
}

juce::Rectangle<int> MetamorfoLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                                             juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append (text, textFont (15.0f), palette::bone);
    juce::TextLayout layout;
    layout.createLayout (s, 320.0f);
    const int w = static_cast<int> (std::ceil (layout.getWidth())) + 18;
    const int h = static_cast<int> (std::ceil (layout.getHeight())) + 10;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void MetamorfoLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> c, float radius, float angle)
{
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb8b49f), c.x - radius * 0.5f, c.y - radius * 0.6f,
                                             juce::Colour (0xff4b4a40), c.x + radius, c.y + radius, true));
    g.fillEllipse (r);
    g.setColour (juce::Colour (0xff1a1b15));
    g.drawEllipse (r, 0.8f);
    juce::Path slot;
    slot.addRectangle (-radius * 0.75f, -radius * 0.13f, radius * 1.5f, radius * 0.26f);
    g.setColour (juce::Colour (0xff23241d));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c));
}

void MetamorfoLookAndFeel::drawLed (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, bool on)
{
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), c.x, c.y, colour.withAlpha (0.0f), c.x + radius * 3.2f, c.y, true));
        g.fillEllipse (r.expanded (radius * 2.2f));
    }
    g.setGradientFill (juce::ColourGradient (on ? colour.brighter (0.6f) : colour.withMultipliedBrightness (0.22f), c.x - radius * 0.3f, c.y - radius * 0.4f,
                                             on ? colour : colour.withMultipliedBrightness (0.12f), c.x + radius, c.y + radius, true));
    g.fillEllipse (r);
    g.setColour (juce::Colour (0xff0d0f0a));
    g.drawEllipse (r, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.55f : 0.12f));
    g.fillEllipse (r.reduced (radius * 0.55f).translated (-radius * 0.25f, -radius * 0.3f));
}

void MetamorfoLookAndFeel::drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                                             const juce::Font& font, juce::Justification just, juce::Colour colour)
{
    g.setFont (font);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawText (text, area.translated (0.0f, 1.0f), just, false);
    g.setColour (colour);
    g.drawText (text, area, just, false);
}

} // namespace metamorfo::ui
