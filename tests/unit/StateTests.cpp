#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using metamorfo::MetamorfoProcessor;
namespace id = metamorfo::params::id;

namespace
{
void setPlain (MetamorfoProcessor& p, const char* paramId, float plainValue)
{
    auto* param = p.getParameters().getParameter (paramId);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

float getPlain (MetamorfoProcessor& p, const char* paramId)
{
    return p.getParameters().getRawParameterValue (paramId)->load();
}
} // namespace

TEST_CASE ("State round-trips every parameter", "[state]")
{
    MetamorfoProcessor source;
    setPlain (source, id::pitch, -3.5f);
    setPlain (source, id::formant, 6.0f);
    setPlain (source, id::link, 1.0f);
    setPlain (source, id::mode, 2.0f);
    setPlain (source, id::drive, 7.25f);
    setPlain (source, id::mix, 42.0f);
    setPlain (source, id::midiReference, 48.0f);
    setPlain (source, id::portamento, 120.0f);

    juce::MemoryBlock blob;
    source.getStateInformation (blob);
    REQUIRE (blob.getSize() > 0);

    MetamorfoProcessor target;
    target.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    CHECK (getPlain (target, id::pitch) == getPlain (source, id::pitch));
    CHECK (getPlain (target, id::formant) == getPlain (source, id::formant));
    CHECK (getPlain (target, id::link) == 1.0f);
    CHECK (getPlain (target, id::mode) == 2.0f);
    CHECK (getPlain (target, id::drive) == getPlain (source, id::drive));
    CHECK (getPlain (target, id::mix) == getPlain (source, id::mix));
    CHECK (getPlain (target, id::midiReference) == 48.0f);
    CHECK (getPlain (target, id::portamento) == getPlain (source, id::portamento));
}

TEST_CASE ("State carries a version number", "[state]")
{
    MetamorfoProcessor p;
    juce::MemoryBlock blob;
    p.getStateInformation (blob);

    const auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->hasTagName (MetamorfoProcessor::kStateType));
    CHECK (xml->getIntAttribute (MetamorfoProcessor::kStateVersionProperty) == MetamorfoProcessor::kStateVersion);
}

TEST_CASE ("State ignores foreign or corrupt data", "[state]")
{
    MetamorfoProcessor p;
    setPlain (p, id::pitch, 5.0f);

    const char garbage[] = "definitely not a Metamorfo state";
    p.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
    p.setStateInformation (nullptr, 0);

    juce::XmlElement other ("SomethingElse");
    juce::MemoryBlock otherBlob;
    juce::AudioProcessor::copyXmlToBinary (other, otherBlob);
    p.setStateInformation (otherBlob.getData(), static_cast<int> (otherBlob.getSize()));

    CHECK (getPlain (p, id::pitch) == 5.0f);
}

TEST_CASE ("Editor can be opened and closed without touching audio", "[ui]")
{
#if JUCE_LINUX
    if (std::getenv ("DISPLAY") == nullptr)
        SKIP ("No X display available");
#endif
    MetamorfoProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 256);
    p.prepareToPlay (48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() > 0);
    // Like every JUCE plugin wrapper: tell the processor before deleting its editor.
    p.editorBeingDeleted (editor.get());
    editor.reset();

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);
    SUCCEED();
}

TEST_CASE ("Link snaps to on/off so a state round-trip restores the same value", "[state][link]")
{
    // pluginval --randomise sets arbitrary normalised values and compares them after
    // getStateInformation / setStateInformation.
    metamorfo::MetamorfoProcessor source;
    auto* link = source.getParameters().getParameter (metamorfo::params::id::link);
    REQUIRE (link->isBoolean());
    CHECK (link->getNumSteps() == 2);
    for (const float raw : { 0.37f, 0.5f, 0.81f, 0.0f, 1.0f })
    {
        link->setValueNotifyingHost (raw);
        const float before = link->getValue();
        CHECK ((before == 0.0f || before == 1.0f));
        juce::MemoryBlock state;
        source.getStateInformation (state);
        metamorfo::MetamorfoProcessor target;
        target.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        CHECK (target.getParameters().getParameter (metamorfo::params::id::link)->getValue() == before);
    }
    CHECK (link->getText (1.0f, 10) == "On");
    CHECK (link->getText (0.0f, 10) == "Off");
}
