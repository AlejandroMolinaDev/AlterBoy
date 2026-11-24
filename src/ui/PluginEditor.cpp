#include "ui/PluginEditor.h"

#include "plugin/PluginProcessor.h"

namespace metamorfo::ui
{
namespace
{
// Source files are UTF-8; JUCE needs to be told explicitly.
juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

juce::String noteToText (int note)
{
    if (note < 0)
        return "--";

    return juce::MidiMessage::getMidiNoteName (note, true, true, 4) + "  (" + juce::String (note) + ")";
}

// "A3 +12¢" for a frequency, "--" when unvoiced.
juce::String pitchToText (float hz)
{
    if (hz <= 0.0f)
        return "--";
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = static_cast<int> (std::lround (midi));
    const int cents = static_cast<int> (std::lround ((midi - nearest) * 100.0));
    return juce::MidiMessage::getMidiNoteName (nearest, true, true, 4)
           + (cents >= 0 ? " +" : " ") + juce::String (cents) + utf8 ("¢");
}

juce::String layoutToText (const juce::AudioChannelSet& set)
{
    if (set == juce::AudioChannelSet::mono())
        return "Mono";
    if (set == juce::AudioChannelSet::stereo())
        return utf8 ("Estéreo");
    return set.getDescription();
}

const juce::Colour kBackground { 0xff1e2318 };
const juce::Colour kText { 0xffe9e4d4 };
const juce::Colour kAccent { 0xff5cf2b0 };
const juce::Colour kDim { 0xff9a9a88 };
} // namespace

MetamorfoEditor::MetamorfoEditor (MetamorfoProcessor& owner)
    : juce::AudioProcessorEditor (owner),
      processor_ (owner)
{
    const auto setup = [this] (juce::Label& label, float height, juce::Colour colour, juce::Justification just) {
        label.setFont (juce::FontOptions { height });
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (just);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    };

    setup (title_, 22.0f, kText, juce::Justification::centredLeft);
    setup (noteCaption_, 14.0f, kDim, juce::Justification::centred);
    setup (noteValue_, 44.0f, kAccent, juce::Justification::centred);
    setup (details_, 15.0f, kText, juce::Justification::centred);
    setup (info_, 13.0f, kDim, juce::Justification::centredLeft);
    setup (pitch_, 15.0f, kAccent, juce::Justification::centred);

    title_.setText ("METAMORFO  " METAMORFO_VERSION_STRING, juce::dontSendNotification);
    noteCaption_.setText (utf8 ("ÚLTIMA NOTA MIDI RECIBIDA"), juce::dontSendNotification);

    setSize (600, 244);
    refreshStaticInfo();
    timerCallback();
    startTimerHz (30);
}

MetamorfoEditor::~MetamorfoEditor()
{
    stopTimer();
}

void MetamorfoEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kDim.withAlpha (0.4f));
    g.drawHorizontalLine (44, 12.0f, static_cast<float> (getWidth()) - 12.0f);
}

void MetamorfoEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    title_.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);
    info_.setBounds (area.removeFromBottom (22));
    pitch_.setBounds (area.removeFromBottom (22));
    noteCaption_.setBounds (area.removeFromTop (20));
    details_.setBounds (area.removeFromBottom (24));
    noteValue_.setBounds (area);
}

void MetamorfoEditor::refreshStaticInfo()
{
    const auto layout = processor_.getBusesLayout();
    const auto text = processor_.getFormatName()
                      + "  |  " + layoutToText (layout.getMainInputChannelSet())
                      + utf8 (" → ") + layoutToText (layout.getMainOutputChannelSet())
                      + "  |  Host: " + juce::PluginHostType().getHostDescription()
                      + "  |  " + juce::String (processor_.getPreparedSampleRate(), 0) + " Hz";
    info_.setText (text, juce::dontSendNotification);
}

void MetamorfoEditor::timerCallback()
{
    // Layout / sample rate can change while the editor is open: refresh ~2x per second.
    if (++ticks_ % 15 == 0)
        refreshStaticInfo();

    const auto& analysis = processor_.getAnalysisDisplay();
    const float inHz = analysis.inputF0.load (std::memory_order_relaxed);
    const float outHz = analysis.outputF0.load (std::memory_order_relaxed);
    pitch_.setText (utf8 ("ENTRADA ") + pitchToText (inHz) + utf8 ("   →   SALIDA ") + pitchToText (outHz),
                    juce::dontSendNotification);

    const auto& activity = processor_.getMidiActivity();
    const int note = activity.lastNote.load (std::memory_order_relaxed);
    const int held = activity.heldNote.load (std::memory_order_relaxed);
    const auto count = activity.eventCount.load (std::memory_order_relaxed);

    if (note == shownNote_ && held == shownHeld_ && count == shownCount_)
        return;

    shownNote_ = note;
    shownHeld_ = held;
    shownCount_ = count;

    noteValue_.setText (noteToText (note), juce::dontSendNotification);
    noteValue_.setColour (juce::Label::textColourId, held >= 0 ? kAccent : kText);

    const auto detailText = utf8 ("Velocidad ") + juce::String (activity.lastVelocity.load (std::memory_order_relaxed))
                            + utf8 ("   ·   Canal ") + juce::String (activity.lastChannel.load (std::memory_order_relaxed))
                            + utf8 ("   ·   Sostenida: ") + noteToText (held)
                            + utf8 ("   ·   Eventos: ") + juce::String (count);
    details_.setText (detailText, juce::dontSendNotification);
}

} // namespace metamorfo::ui
