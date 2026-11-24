#include "ui/components/ToggleLever3.h"

#include "ui/MetamorfoLookAndFeel.h"

namespace metamorfo::ui
{

ToggleLever3::ToggleLever3 (juce::RangedAudioParameter& param, juce::StringArray labels, MetamorfoLookAndFeel& lnf)
    : param_ (param), labels_ (std::move (labels)), lnf_ (lnf)
{
    attachment_ = std::make_unique<juce::ParameterAttachment> (param_, [this] (float plain) {
        position_ = std::clamp (static_cast<int> (std::lround (plain)), 0, 2);
        repaint();
    });
    attachment_->sendInitialUpdate();
}

ToggleLever3::~ToggleLever3() = default;

juce::Point<float> ToggleLever3::pivot() const
{
    return { 34.0f, static_cast<float> (getHeight()) * 0.5f };
}

float ToggleLever3::labelY (int index) const
{
    const float h = static_cast<float> (getHeight());
    return h * (0.18f + 0.32f * static_cast<float> (index));
}

int ToggleLever3::indexForY (float y) const
{
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs (y - labelY (i)) < std::abs (y - labelY (best)))
            best = i;
    return best;
}

void ToggleLever3::select (int index)
{
    index = std::clamp (index, 0, 2);
    if (index == position_)
        return;
    attachment_->setValueAsCompleteGesture (static_cast<float> (index));
}

void ToggleLever3::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto p = pivot();

    // Engraved guide slot.
    const auto slot = juce::Rectangle<float> (12.0f, labelY (2) - labelY (0) + 18.0f).withCentre (p);
    g.setColour (engraving);
    g.fillRoundedRectangle (slot, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (slot.translated (0.0f, 1.0f), 6.0f, 1.0f);

    // Hex nut + base plate.
    juce::Path nut;
    nut.addPolygon (p, 6, 15.0f, juce::MathConstants<float>::pi / 6.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9c6b4), p.x - 10.0f, p.y - 12.0f, juce::Colour (0xff55534a), p.x + 12.0f, p.y + 12.0f, false));
    g.fillPath (nut);
    g.setColour (juce::Colour (0xff1a1a16));
    g.strokePath (nut, juce::PathStrokeType (1.0f));

    // Lever: tapered chrome bat from the pivot towards the selected position.
    const float target = labelY (position_);
    const auto tip = juce::Point<float> (p.x, target);
    juce::Path lever;
    if (position_ == 1)
    {
        // Centre position: lever points at the viewer (seen end-on as a round cap).
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f0e6), p.x - 6.0f, p.y - 7.0f, juce::Colour (0xff6b6a62), p.x + 6.0f, p.y + 7.0f, true));
        g.fillEllipse (juce::Rectangle<float> (17.0f, 17.0f).withCentre (p));
        g.setColour (juce::Colour (0xff161613));
        g.drawEllipse (juce::Rectangle<float> (17.0f, 17.0f).withCentre (p), 1.0f);
    }
    else
    {
        lever.startNewSubPath (p.x - 4.0f, p.y);
        lever.lineTo (tip.x - 6.5f, tip.y);
        lever.lineTo (tip.x + 6.5f, tip.y);
        lever.lineTo (p.x + 4.0f, p.y);
        lever.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeadc), p.x - 7.0f, p.y, juce::Colour (0xff5d5c55), p.x + 7.0f, p.y, false));
        g.fillPath (lever);
        g.setColour (juce::Colour (0xff161613));
        g.strokePath (lever, juce::PathStrokeType (1.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f5ec), tip.x - 5.0f, tip.y - 4.0f, juce::Colour (0xff6b6a62), tip.x + 6.0f, tip.y + 5.0f, true));
        g.fillEllipse (juce::Rectangle<float> (15.0f, 10.0f).withCentre (tip));
        g.setColour (juce::Colour (0xff161613));
        g.drawEllipse (juce::Rectangle<float> (15.0f, 10.0f).withCentre (tip), 1.0f);
    }

    // LEDs + engraved labels.
    for (int i = 0; i < 3; ++i)
    {
        const float y = labelY (i);
        MetamorfoLookAndFeel::drawLed (g, { 66.0f, y }, 4.5f, phosphor, i == position_);
        MetamorfoLookAndFeel::drawEngravedText (g, labels_[i], { 78.0f, y - 10.0f, static_cast<float> (getWidth()) - 78.0f, 20.0f },
                                                lnf_.labelFont (17.0f), juce::Justification::centredLeft,
                                                i == position_ ? bone : boneDim);
    }
}

void ToggleLever3::mouseDown (const juce::MouseEvent& e)
{
    dragging_ = std::abs (e.position.x - pivot().x) < 22.0f;
    if (! dragging_)
        select (indexForY (e.position.y)); // click on a label / LED
}

void ToggleLever3::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging_)
        select (indexForY (e.position.y));
}

void ToggleLever3::mouseUp (const juce::MouseEvent&)
{
    dragging_ = false;
}

} // namespace metamorfo::ui
