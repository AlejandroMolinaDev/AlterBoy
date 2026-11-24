#include "ui/PluginEditor.h"

#include <algorithm>
#include <cmath>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace metamorfo::ui
{
namespace
{
namespace id = params::id;

juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

// "A3 +12¢" for a frequency, "--" when unvoiced.
juce::String pitchToText (float hz)
{
    if (hz <= 0.0f)
        return "-- ";
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = static_cast<int> (std::lround (midi));
    const int cents = static_cast<int> (std::lround ((midi - nearest) * 100.0));
    return juce::MidiMessage::getMidiNoteName (nearest, true, true, 4) + (cents >= 0 ? " +" : " ") + juce::String (cents) + utf8 ("¢");
}

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Metamorfo";
    o.folderName = "Metamorfo";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

Knob::Scale bipolarScale()
{
    return { { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f }, { utf8 ("−12"), utf8 ("−6"), "0", "+6", "+12" }, 1.0f };
}
} // namespace

// =======================================================================================
class MetamorfoEditor::Content final : public juce::Component
{
public:
    Content (MetamorfoProcessor& p, MetamorfoLookAndFeel& lnf)
        : processor_ (p), lnf_ (lnf),
          presetBar_ (p.getPresets(), p.getParameters(), lnf),
          pitch_ (p.getParameters(), p.getLocks(), id::pitch, "PITCH", bipolarScale(), lnf),
          formant_ (p.getParameters(), p.getLocks(), id::formant, "FORMANT", bipolarScale(), lnf),
          drive_ (p.getParameters(), p.getLocks(), id::drive, "DRIVE",
                  { { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f }, { "0", "3", "6", "9", "12" }, 1.0f }, lnf),
          mix_ (p.getParameters(), p.getLocks(), id::mix, "MIX",
                { { 0.0f, 50.0f, 100.0f }, { "DRY", "50", "WET" }, 10.0f }, lnf),
          lever_ (*p.getParameters().getParameter (id::mode), { "TRANSPONER", "CUANTIZAR", "ROBOT" }, lnf),
          linkAttachment_ (*p.getParameters().getParameter (id::link), link_, nullptr)
    {
        setSize (kBaseWidth, kBaseHeight);
        buildTexture();

        for (auto* c : std::initializer_list<juce::Component*> { &presetBar_, &pitch_, &formant_, &drive_, &mix_, &lever_, &link_, &help_ })
            addAndMakeVisible (c);
        help_.setVisible (false);

        link_.setClickingTogglesState (true);
        link_.setButtonText ("LINK");
        link_.onStateChange = [this] { repaint(); };
        link_.setLookAndFeel (&linkLook_);

        pitch_.setTooltip (utf8 ("PITCH: transposición de −12 a +12 semitonos (resolución 0,1). En Robot mueve la nota en semitonos enteros. "
                                 "Doble clic: 0 · Shift/Cmd + arrastrar: ajuste fino · clic derecho: escribir valor / bloquear"));
        formant_.setTooltip (utf8 ("FORMANT: timbre de la voz sin cambiar la altura. Negativo = más grueso, positivo = más claro. "
                                   "−1 a −3 da cuerpo a los coros. Con LINK refleja el valor de PITCH"));
        drive_.setTooltip (utf8 ("DRIVE: saturación de válvula tipo tríodo (0 = transparente). El nivel se compensa solo"));
        mix_.setTooltip (utf8 ("MIX: mezcla entre la voz original (DRY) y la procesada (WET), alineadas en el tiempo"));
        link_.setTooltip (utf8 ("LINK: el formante sigue a PITCH (cambio tipo cinta / DJ pitch)"));
        lever_.setTooltip (utf8 ("Modo: TRANSPONER desplaza la voz · CUANTIZAR la lleva al semitono más cercano (hard-tune) · "
                                 "ROBOT la fija en una nota (Do5 o la nota MIDI recibida)"));

        presetBar_.onShowMidiHelp = [this] { help_.setVisible (true); help_.toFront (false); };
    }

    ~Content() override { link_.setLookAndFeel (nullptr); }

    PresetBar& presetBar() { return presetBar_; }

    void refresh()
    {
        presetBar_.refresh();
        for (auto* k : { &pitch_, &formant_, &drive_, &mix_ })
            k->refresh();

        // FORMANT mirrors the linked value while LINK is on (SPEC §4.2).
        if (link_.getToggleState())
        {
            auto* pitchParam = processor_.getParameters().getRawParameterValue (id::pitch);
            formant_.setMirror ([pitchParam] { return pitchParam->load(); }, "FORMANT = PITCH");
        }
        else
        {
            formant_.clearMirror();
        }

        const auto& a = processor_.getAnalysisDisplay();
        const auto in = pitchToText (a.inputF0.load (std::memory_order_relaxed));
        const auto out = pitchToText (a.outputF0.load (std::memory_order_relaxed));
        const int held = processor_.getMidiActivity().heldNote.load (std::memory_order_relaxed);
        if (in != shownIn_ || out != shownOut_ || held != shownHeld_)
        {
            shownIn_ = in;
            shownOut_ = out;
            shownHeld_ = held;
            repaint (displayArea().expanded (2.0f).toNearestInt());
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace palette;
        const auto r = getLocalBounds().toFloat();

        // Powder-coated olive panel.
        g.setGradientFill (juce::ColourGradient (panelTop, 0.0f, 0.0f, panelBottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
        g.drawImage (texture_, r);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (3.0f), 8.0f, 1.0f);

        for (const auto& c : { juce::Point<float> (13.0f, 13.0f), { r.getRight() - 13.0f, 13.0f },
                                { 13.0f, r.getBottom() - 13.0f }, { r.getRight() - 13.0f, r.getBottom() - 13.0f } })
            MetamorfoLookAndFeel::drawScrew (g, c, 5.5f, 0.6f + c.x * 0.01f);

        // Engraved separators between the three blocks.
        for (const float x : { 312.0f, 482.0f })
        {
            g.setColour (engraving.withAlpha (0.9f));
            g.drawLine (x, 52.0f, x, 246.0f, 1.2f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawLine (x + 1.2f, 52.0f, x + 1.2f, 246.0f, 1.0f);
        }

        // Block titles.
        const auto title = lnf_.labelFont (13.0f);
        MetamorfoLookAndFeel::drawEngravedText (g, utf8 ("TONO  ·  FORMANTE"), { 14.0f, 46.0f, 292.0f, 14.0f }, title, juce::Justification::centred, boneDim);
        MetamorfoLookAndFeel::drawEngravedText (g, utf8 ("COLOR  ·  MEZCLA"), { 490.0f, 46.0f, 274.0f, 14.0f }, title, juce::Justification::centred, boneDim);

        // LINK connecting line between PITCH and FORMANT when active.
        if (link_.getToggleState())
        {
            g.setColour (cyan.withAlpha (0.8f));
            const float y = 128.0f;
            g.drawLine (126.0f, y, 151.0f, y, 2.0f);
            g.drawLine (169.0f, y, 194.0f, y, 2.0f);
        }

        drawLogo (g, { 318.0f, 46.0f, 162.0f, 34.0f });
        drawDisplay (g);
    }

    void resized() override
    {
        presetBar_.setBounds (40, 10, kBaseWidth - 80, 26);
        // Left block: knobs 164 px apart so LINK has room between the ±6 marks.
        pitch_.setBounds (14, 64, 128, 176);
        formant_.setBounds (178, 64, 128, 176);
        link_.setBounds (138, 122, 44, 26);
        lever_.setBounds (322, 84, 156, 104);
        drive_.setBounds (490, 64, 128, 176);
        mix_.setBounds (636, 64, 128, 176);
        help_.setBounds (getLocalBounds());
    }

private:
    // LINK: LED + engraved label, no button chrome.
    struct LinkLook final : public juce::LookAndFeel_V4
    {
        explicit LinkLook (MetamorfoLookAndFeel& l) : lnf (l) {}
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool) override
        {
            MetamorfoLookAndFeel::drawLed (g, { static_cast<float> (b.getWidth()) * 0.5f, 6.0f }, 4.5f, palette::cyan, b.getToggleState());
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            MetamorfoLookAndFeel::drawEngravedText (g, b.getButtonText(), b.getLocalBounds().toFloat().withTrimmedTop (11.0f),
                                                    lnf.labelFont (14.0f), juce::Justification::centred,
                                                    b.getToggleState() ? palette::cyan : palette::bone);
        }
        MetamorfoLookAndFeel& lnf;
    };

    [[nodiscard]] juce::Rectangle<float> displayArea() const { return { 320.0f, 196.0f, 154.0f, 48.0f }; }

    void buildTexture()
    {
        // Powder-coat grain: fixed-seed noise, generated once at 2x the base size.
        texture_ = juce::Image (juce::Image::ARGB, kBaseWidth * 2, kBaseHeight * 2, true);
        juce::Random rng (1234);
        juce::Image::BitmapData data (texture_, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < texture_.getHeight(); ++y)
            for (int x = 0; x < texture_.getWidth(); ++x)
            {
                const float v = rng.nextFloat();
                const auto alpha = static_cast<juce::uint8> (v < 0.5f ? 28.0f * v : 20.0f * (1.0f - v));
                data.setPixelColour (x, y, (v < 0.5f ? juce::Colours::black : juce::Colours::white).withAlpha (alpha));
            }
    }

    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // Small morphing-wave icon: a sine that turns into a square wave.
        const auto icon = area.removeFromLeft (26.0f).reduced (2.0f, 8.0f);
        juce::Path wave;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = static_cast<float> (i) / 40.0f;
            const float s = std::sin (t * juce::MathConstants<float>::twoPi * 1.5f);
            const float sq = s >= 0.0f ? 1.0f : -1.0f;
            const float v = (1.0f - t) * s + t * (0.85f * sq + 0.15f * s); // morph sine -> square
            const juce::Point<float> p (icon.getX() + t * icon.getWidth(), icon.getCentreY() - v * icon.getHeight() * 0.45f);
            if (i == 0)
                wave.startNewSubPath (p);
            else
                wave.lineTo (p);
        }
        g.setColour (palette::phosphor);
        g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        MetamorfoLookAndFeel::drawEngravedText (g, "METAMORFO", area, lnf_.labelFont (27.0f).withExtraKerningFactor (0.12f),
                                                juce::Justification::centredLeft, palette::bone);
    }

    void drawDisplay (juce::Graphics& g) const
    {
        using namespace palette;
        const auto r = displayArea();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 4.0f);
        g.setColour (glass);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (phosphor.withAlpha (0.06f));
        for (float y = r.getY() + 2.0f; y < r.getBottom(); y += 3.0f) // scan lines
            g.drawHorizontalLine (static_cast<int> (y), r.getX() + 2.0f, r.getRight() - 2.0f);

        auto inner = r.reduced (7.0f, 4.0f);
        g.setFont (lnf_.displayFont (14.5f));
        g.setColour (phosphor);
        g.drawText ("IN  " + shownIn_, inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        g.drawText ("OUT " + shownOut_, inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);

        auto& params = processor_.getParameters();
        const int ref = juce::roundToInt (params.getRawParameterValue (id::midiReference)->load());
        const int porta = juce::roundToInt (params.getRawParameterValue (id::portamento)->load());
        g.setFont (lnf_.displayFont (10.5f));
        g.setColour (phosphor.withAlpha (0.6f));
        const auto midi = shownHeld_ >= 0 ? juce::String ("MIDI ") + juce::MidiMessage::getMidiNoteName (shownHeld_, true, true, 4)
                                          : juce::String ("MIDI --");
        g.drawText (midi + "  REF " + juce::MidiMessage::getMidiNoteName (ref, true, true, 4) + "  PORT " + juce::String (porta),
                    inner, juce::Justification::centredLeft, false);
    }

    MetamorfoProcessor& processor_;
    MetamorfoLookAndFeel& lnf_;
    PresetBar presetBar_;
    Knob pitch_, formant_, drive_, mix_;
    ToggleLever3 lever_;
    juce::TextButton link_;
    LinkLook linkLook_ { lnf_ };
    juce::ButtonParameterAttachment linkAttachment_;
    MidiHelpScreen help_ { lnf_ };
    juce::Image texture_;
    juce::String shownIn_ { "-- " }, shownOut_ { "-- " };
    int shownHeld_ = -2;
};

// =======================================================================================
MetamorfoEditor::MetamorfoEditor (MetamorfoProcessor& owner)
    : juce::AudioProcessorEditor (owner)
{
    setLookAndFeel (&lnf_);
    content_ = std::make_unique<Content> (owner, lnf_);
    addAndMakeVisible (*content_);
    tooltips_.setLookAndFeel (&lnf_);

    content_->presetBar().currentScale = [this] { return currentScale(); };
    content_->presetBar().onScaleChosen = [this] (float s) {
        applyScale (s);
        saveScale (s);
    };

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);
    applyScale (loadSavedScale());

    content_->refresh(); // correct state before the first timer tick
    startTimerHz (30);
}

MetamorfoEditor::~MetamorfoEditor()
{
    stopTimer();
    saveScale (currentScale());
    tooltips_.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

float MetamorfoEditor::loadSavedScale()
{
    juce::PropertiesFile settings (settingsOptions());
    return std::clamp (static_cast<float> (settings.getDoubleValue ("uiScale", 1.5)), 0.75f, 2.0f);
}

void MetamorfoEditor::saveScale (float scale)
{
    juce::PropertiesFile settings (settingsOptions());
    settings.setValue ("uiScale", std::clamp (scale, 0.75f, 2.0f));
    settings.saveIfNeeded();
}

float MetamorfoEditor::currentScale() const
{
    // Width AND height (B24): a host that imposes a size with another aspect ratio must
    // never crop the canvas; the content is scaled to fit and centred.
    return std::min (static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth),
                     static_cast<float> (getHeight()) / static_cast<float> (kBaseHeight));
}

void MetamorfoEditor::applyScale (float scale)
{
    scale = std::clamp (scale, 0.75f, 2.0f);
    setSize (juce::roundToInt (static_cast<float> (kBaseWidth) * scale), juce::roundToInt (static_cast<float> (kBaseHeight) * scale));
}

void MetamorfoEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::panelEdge);
}

void MetamorfoEditor::resized()
{
    if (content_ == nullptr)
        return;
    const float scale = currentScale();
    const float x = 0.5f * (static_cast<float> (getWidth()) - scale * static_cast<float> (kBaseWidth));
    const float y = 0.5f * (static_cast<float> (getHeight()) - scale * static_cast<float> (kBaseHeight));
    content_->setTransform (juce::AffineTransform::scale (scale).translated (std::round (x), std::round (y)));
    content_->setBounds (0, 0, kBaseWidth, kBaseHeight);
    content_->presetBar().refresh(); // scale label up to date at once, not on the next tick (B25)
}

void MetamorfoEditor::timerCallback()
{
    content_->refresh();
}

} // namespace metamorfo::ui
