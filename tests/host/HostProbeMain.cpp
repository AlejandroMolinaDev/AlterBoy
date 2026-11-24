// MetamorfoHostProbe: loads the BUILT plugin binaries the way a DAW does and checks
// the host-facing contract that FL Studio relies on (effect, MIDI input, layouts,
// processing, state). VST3 via JUCE's hosting, CLAP via a minimal raw-API host.
//
// Usage: MetamorfoHostProbe vst3 <path/to/Metamorfo.vst3>
//        MetamorfoHostProbe clap <path/to/Metamorfo.clap>

#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "ProbeCheck.h"

namespace probe
{
namespace
{
using Set = juce::AudioChannelSet;

juce::AudioProcessor::BusesLayout layout (const Set& in, const Set& out)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in);
    l.outputBuses.add (out);
    return l;
}

float signal (int ch, int i)
{
    return 0.4f * std::sin (0.021f * static_cast<float> (i) * static_cast<float> (ch + 1));
}
} // namespace

int runVst3Probe (const std::string& path)
{
    std::cout << "VST3 probe: " << path << std::endl;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::String (path));

    if (! check (types.size() == 1, "scan finds exactly one plugin"))
        return 1;

    const auto& desc = *types[0];
    check (desc.name == "Metamorfo", "name is 'Metamorfo' (" + desc.name.toStdString() + ")");
    check (desc.manufacturerName == "Metamorfo Audio",
           "manufacturer is 'Metamorfo Audio' (" + desc.manufacturerName.toStdString() + ")");
    check (! desc.isInstrument, "registered as an effect, not an instrument");
    check (desc.category.contains ("Fx"), "VST3 category contains Fx (" + desc.category.toStdString() + ")");

    juce::String error;
    auto instance = format.createInstanceFromDescription (desc, 48000.0, 512, error);
    if (! check (instance != nullptr, "instance created " + error.toStdString()))
        return 1;

    check (instance->acceptsMidi(), "accepts MIDI (event input bus present: FL 'MIDI input port' works)");
    check (! instance->producesMidi(), "does not produce MIDI");
    check (instance->checkBusesLayoutSupported (layout (Set::mono(), Set::mono())), "layout mono -> mono");
    check (instance->checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())), "layout mono -> stereo");
    check (instance->checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())), "layout stereo -> stereo");
    check (! instance->checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())), "layout stereo -> mono rejected");
    check (instance->getParameters().size() >= 8, "exposes at least the 8 frozen parameters");

    check (instance->setBusesLayout (layout (Set::stereo(), Set::stereo())), "set stereo layout");
    instance->setRateAndBufferSizeDetails (48000.0, 512);
    instance->prepareToPlay (48000.0, 512);
    const int latency = instance->getLatencySamples();
    check (latency == 2 * static_cast<int> (std::ceil (48000.0 / 60.0)) + 3 /* drive oversampling */,
           "reported latency = 2 x 60 Hz period + 3 (" + std::to_string (latency) + " samples)");

    // Process audio + MIDI through the real binary; output must equal input (passthrough).
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    float maxError = 0.0f;
    bool finite = true;
    for (int block = 0; block < 100; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, signal (ch, block * 512 + i));
        // Blocks 0-59: no MIDI (neutral -> delayed input). Blocks 60-99: MIDI notes, which
        // now transpose the voice, so only finiteness is checked there.
        midi.clear();
        if (block >= 60 && block % 10 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60 + block / 10, static_cast<juce::uint8> (100)), 17);
        if (block >= 60 && block % 10 == 5) midi.addEvent (juce::MidiMessage::noteOff (1, 60 + block / 10), 3);

        instance->processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                // Neutral settings: input delayed by the reported latency.
                const float y = buffer.getSample (ch, i);
                const int n = block * 512 + i - latency;
                finite &= std::isfinite (y) && std::abs (y) < 4.0f;
                if (block < 60)
                    maxError = std::max (maxError, std::abs (y - (n >= 0 ? signal (ch, n) : 0.0f)));
            }
    }
    check (finite, "processing with MIDI produces finite output");
    check (maxError < 1.0e-6f, "neutral settings = delayed input through the binary (max error " + std::to_string (maxError) + ")");

    // State round trip through the host interface.
    auto* firstParam = instance->getParameters()[0];
    firstParam->setValueNotifyingHost (0.75f);
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    check (state.getSize() > 0, "state saved through the host");

    auto second = format.createInstanceFromDescription (desc, 48000.0, 512, error);
    if (check (second != nullptr, "second instance created"))
    {
        second->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (std::abs (second->getParameters()[0]->getValue() - firstParam->getValue()) < 1.0e-6f,
               "state restored into a new instance");
    }

    // Host bypass (B19/B20): with the plug-in's bypass parameter on, the output must be the
    // input delayed by the reported latency, even with non-neutral settings (pitch +6 above).
    if (auto* bypass = instance->getBypassParameter(); check (bypass != nullptr, "exposes a host bypass parameter"))
    {
        bypass->setValueNotifyingHost (1.0f);
        float bypassError = 0.0f;
        for (int block = 100; block < 160; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, signal (ch, block * 512 + i));
            midi.clear();
            instance->processBlock (buffer, midi);
            if (block < 104) // the bypass change is applied at the next process call
                continue;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    bypassError = std::max (bypassError, std::abs (buffer.getSample (ch, i) - signal (ch, block * 512 + i - latency)));
        }
        check (bypassError < 1.0e-6f, "bypassed = input delayed by the latency (max error " + std::to_string (bypassError) + ")");
        bypass->setValueNotifyingHost (0.0f);
    }

    // State saved at 48 kHz restored into an instance running at 96 kHz.
    auto other = format.createInstanceFromDescription (desc, 96000.0, 256, error);
    if (check (other != nullptr, "instance at 96 kHz created"))
    {
        other->setRateAndBufferSizeDetails (96000.0, 256);
        other->prepareToPlay (96000.0, 256);
        other->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (std::abs (other->getParameters()[0]->getValue() - firstParam->getValue()) < 1.0e-6f, "state restored at another sample rate");
        check (other->getLatencySamples() == 2 * static_cast<int> (std::ceil (96000.0 / 60.0)) + 3, "latency scales with the sample rate (96 kHz)");
        other->releaseResources();
    }

    // Many instances of the binary at once (FL Studio projects with many vocal tracks).
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> many;
    for (int k = 0; k < 12; ++k)
        if (auto inst = format.createInstanceFromDescription (desc, 48000.0, 256, error))
        {
            inst->setRateAndBufferSizeDetails (48000.0, 256);
            inst->prepareToPlay (48000.0, 256);
            many.push_back (std::move (inst));
        }
    check (many.size() == 12, "12 instances created");
    bool manyFinite = true;
    juce::AudioBuffer<float> small (2, 256);
    for (int block = 0; block < 40; ++block)
        for (std::size_t k = 0; k < many.size(); ++k)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    small.setSample (ch, i, signal (ch + static_cast<int> (k), block * 256 + i));
            midi.clear();
            many[k]->processBlock (small, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    manyFinite &= std::isfinite (small.getSample (ch, i));
        }
    check (manyFinite, "12 instances process without problems");
    for (auto& inst : many)
        inst->releaseResources();

    instance->releaseResources();
    return failures() == 0 ? 0 : 1;
}
} // namespace probe

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc != 3)
    {
        std::cerr << "usage: MetamorfoHostProbe vst3|clap <plugin path>" << std::endl;
        return 2;
    }

    const std::string kind = argv[1];
    const int result = kind == "vst3" ? probe::runVst3Probe (argv[2])
                     : kind == "clap" ? probe::runClapProbe (argv[2])
                                      : 2;
    std::cout << (result == 0 ? "PROBE OK" : "PROBE FAILED") << " (" << probe::failures() << " failures)" << std::endl;
    return result;
}
