#include "ui/components/Knob.h"

#include "ui/MetamorfoLookAndFeel.h"

namespace metamorfo::ui
{

// Invisible slider covering the knob body: provides drag, wheel and double-click.
class Knob::KnobSlider final : public juce::Slider
{
public:
    KnobSlider() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setRotaryParameters (kStartAngle + juce::MathConstants<float>::twoPi, kEndAngle + juce::MathConstants<float>::twoPi, true);
        setMouseDragSensitivity (220);
        setScrollWheelEnabled (true);
        setPopupMenuEnabled (false);
    }

    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick)
                onRightClick();
            return;
        }
        // Fine adjustment: Shift (Windows) or Cmd (macOS), decided when the drag starts.
        const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown();
        setMouseDragSensitivity (fine ? 1400 : 220);
        juce::Slider::mouseDown (e.withNewPosition (e.position)); // keep JUCE's handling
    }

    void paint (juce::Graphics&) override {} // drawn by the Knob
};

Knob::Knob (juce::AudioProcessorValueTreeState& state, ParameterLocks& locks, const juce::String& paramId,
            const juce::String& caption, Scale scale, MetamorfoLookAndFeel& lnf)
    : state_ (state), locks_ (locks), paramId_ (paramId), caption_ (caption), scale_ (std::move (scale)), lnf_ (lnf)
{
    param_ = state_.getParameter (paramId_);
    jassert (param_ != nullptr);

    slider_ = std::make_unique<KnobSlider>();
    slider_->onValueChange = [this] { repaint(); };
    slider_->onRightClick = [this] { showContextMenu(); };
    addAndMakeVisible (*slider_);
    attachment_ = std::make_unique<juce::SliderParameterAttachment> (*param_, *slider_, nullptr);

    const auto defaultPlain = param_->convertFrom0to1 (param_->getDefaultValue());
    slider_->setDoubleClickReturnValue (true, defaultPlain);
}

Knob::~Knob()
{
    attachment_.reset();
}

void Knob::setMirror (std::function<float()> mirrorValue, const juce::String& mirrorCaption)
{
    mirror_ = std::move (mirrorValue);
    mirrorCaption_ = mirrorCaption;
    slider_->setInterceptsMouseClicks (false, false);
    repaint();
}

void Knob::clearMirror()
{
    if (! mirror_)
        return;
    mirror_ = nullptr;
    slider_->setInterceptsMouseClicks (true, true);
    repaint();
}

void Knob::refresh()
{
    const bool locked = locks_.isLocked (paramId_);
    if (locked != locked_ || mirror_)
    {
        locked_ = locked;
        repaint();
    }
}

juce::Point<float> Knob::centre() const
{
    return { static_cast<float> (getWidth()) * 0.5f, (static_cast<float> (getHeight()) - 24.0f) * 0.5f };
}

float Knob::radius() const
{
    return std::min (static_cast<float> (getWidth()), static_cast<float> (getHeight()) - 24.0f) * 0.5f;
}

float Knob::angleFor (float plainValue) const
{
    const float norm = param_->getNormalisableRange().convertTo0to1 (plainValue);
    return kStartAngle + norm * (kEndAngle - kStartAngle);
}

juce::Rectangle<float> Knob::captionArea() const
{
    return getLocalBounds().toFloat().removeFromBottom (24.0f);
}

void Knob::resized()
{
    const float r = radius() * 0.70f; // body (inside the scale ring)
    slider_->setBounds (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre()).toNearestInt());
}

void Knob::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto c = centre();
    const float outer = radius();
    const float body = outer * 0.70f;
    const auto range = param_->getNormalisableRange();

    // ---- scale ring -------------------------------------------------------------------
    g.setColour (bone.withAlpha (0.85f));
    if (scale_.minorStep > 0.0f)
        for (float v = range.start; v <= range.end + 1.0e-4f; v += scale_.minorStep)
        {
            const float a = angleFor (v);
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (outer * 0.76f, a), c.getPointOnCircumference (outer * 0.82f, a)), 1.0f);
        }
    for (std::size_t i = 0; i < scale_.majorTicks.size(); ++i)
    {
        const float a = angleFor (scale_.majorTicks[i]);
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (outer * 0.75f, a), c.getPointOnCircumference (outer * 0.87f, a)), 1.8f);
        const auto p = c.getPointOnCircumference (outer * 1.0f, a);
        MetamorfoLookAndFeel::drawEngravedText (g, scale_.majorLabels[i], juce::Rectangle<float> (34.0f, 14.0f).withCentre (p),
                                                lnf_.labelFont (12.5f), juce::Justification::centred, boneDim);
    }

    // ---- cylinder body (cream, smooth, no fluting) -------------------------------------
    const auto shadowArea = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c.translated (0.0f, body * 0.10f));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (shadowArea.expanded (body * 0.06f));
    const auto side = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c.translated (0.0f, body * 0.06f));
    g.setGradientFill (juce::ColourGradient (creamShade, side.getX(), c.y, creamShade.darker (0.35f), side.getRight(), c.y, false));
    g.fillEllipse (side);
    const auto top = juce::Rectangle<float> (body * 1.92f, body * 1.92f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (cream.brighter (0.15f), top.getX(), top.getY(), creamShade, top.getRight(), top.getBottom(), false));
    g.fillEllipse (top);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (top, 0.8f);

    // ---- value / indicator ---------------------------------------------------------------
    const bool mirrored = static_cast<bool> (mirror_);
    const float shown = mirrored ? std::clamp (mirror_(), range.start, range.end) : param_->convertFrom0to1 (param_->getValue());
    const float angle = angleFor (shown);

    // Black indicator line on the cream body.
    g.setColour (cap);
    g.drawLine (juce::Line<float> (c.getPointOnCircumference (body * 0.62f, angle), c.getPointOnCircumference (body * 0.93f, angle)), 3.0f);

    // Black cap.
    const auto capArea = juce::Rectangle<float> (body * 1.1f, body * 1.1f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), capArea.getX(), capArea.getY(), cap, capArea.getCentreX(), capArea.getCentreY(), true));
    g.fillEllipse (capArea);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (capArea.reduced (1.5f), 1.0f);

    if (mirrored)
    {
        // Linked: a cyan arc around the scale shows the mirrored value.
        juce::Path arc;
        const float zero = angleFor (std::clamp (0.0f, range.start, range.end));
        arc.addCentredArc (c.x, c.y, outer * 0.70f, outer * 0.70f, 0.0f, std::min (zero, angle), std::max (zero, angle), true);
        g.setColour (cyan.withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- caption (name or value) --------------------------------------------------------
    juce::String text = caption_;
    if (mirrored)
        text = mirrorCaption_;
    else if (showValue_)
        text = param_->getCurrentValueAsText();
    const auto colour = locked_ ? lockRed : bone;
    MetamorfoLookAndFeel::drawEngravedText (g, text, captionArea(), lnf_.labelFont (18.0f), juce::Justification::centred, colour);
    if (locked_)
    {
        // Small padlock next to the label.
        const auto a = captionArea();
        const float x = a.getCentreX() + juce::GlyphArrangement::getStringWidth (lnf_.labelFont (18.0f), text) * 0.5f + 6.0f;
        const float y = a.getCentreY();
        g.setColour (lockRed);
        g.fillRoundedRectangle (x, y - 1.0f, 8.0f, 6.0f, 1.0f);
        juce::Path shackle;
        shackle.addCentredArc (x + 4.0f, y - 1.0f, 2.6f, 3.0f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.3f));
    }
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (mirror_)
        return;

    if (captionArea().contains (e.position))
    {
        if (e.mods.isPopupMenu())
            showContextMenu();
        else
        {
            showValue_ = ! showValue_;
            repaint();
        }
        return;
    }

    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    // Click on the scale ring: jump to the nearest mark.
    const auto c = centre();
    const float d = e.position.getDistanceFrom (c);
    if (d < radius() * 0.72f || d > radius() * 1.12f)
        return;
    const float clickAngle = std::atan2 (e.position.x - c.x, c.y - e.position.y);

    float best = 0.0f, bestDistance = 1.0e9f;
    const auto consider = [&] (float v) {
        const float distance = std::abs (angleFor (v) - clickAngle);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = v;
        }
    };
    for (const float v : scale_.majorTicks)
        consider (v);
    const auto range = param_->getNormalisableRange();
    if (scale_.minorStep > 0.0f)
        for (float v = range.start; v <= range.end + 1.0e-4f; v += scale_.minorStep)
            consider (v);
    if (bestDistance < 0.2f)
        slider_->setValue (best, juce::sendNotificationSync);
}

void Knob::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (caption_);
    menu.addItem (1, juce::String::fromUTF8 ("Escribir valor…"), ! static_cast<bool> (mirror_));
    menu.addItem (2, locks_.isLocked (paramId_) ? "Desbloquear" : "Bloquear", true, locks_.isLocked (paramId_));
    menu.addItem (3, "Valor por defecto", ! static_cast<bool> (mirror_));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<Knob> (this)] (int result) {
                            if (safe == nullptr)
                                return;
                            if (result == 1)
                                safe->promptForValue();
                            else if (result == 2)
                            {
                                safe->locks_.toggle (safe->paramId_);
                                safe->refresh();
                            }
                            else if (result == 3)
                                safe->slider_->setValue (safe->param_->convertFrom0to1 (safe->param_->getDefaultValue()),
                                                         juce::sendNotificationSync);
                        });
}

void Knob::promptForValue()
{
    auto* window = new juce::AlertWindow (caption_, juce::String::fromUTF8 ("Escribe el valor exacto:"), juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("value", param_->getCurrentValueAsText());
    window->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->setLookAndFeel (&lnf_);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<Knob> (this), window] (int result) {
                                 if (safe == nullptr || result != 1)
                                     return;
                                 const auto text = window->getTextEditorContents ("value");
                                 const float normalised = safe->param_->getValueForText (text);
                                 safe->slider_->setValue (safe->param_->convertFrom0to1 (normalised), juce::sendNotificationSync);
                             }),
                             true);
}

} // namespace metamorfo::ui
