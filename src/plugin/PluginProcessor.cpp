#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "plugin/Parameters.h"
#include "ui/PluginEditor.h"

namespace metamorfo
{

MetamorfoProcessor::MetamorfoProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_ (*this, nullptr, kStateType, params::createLayout())
{
}

void MetamorfoProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedSampleRate_.store (sampleRate, std::memory_order_relaxed);
    preparedBlockSize_.store (std::max (1, maximumExpectedSamplesPerBlock), std::memory_order_relaxed);

    midiPitch_.reset();
    midiActivity_.heldNote.store (-1, std::memory_order_relaxed);

    // Latency is fixed per sample rate and always reported (FL Studio PDC).
    setLatencySamples (getEngineLatencySamples());
}

void MetamorfoProcessor::releaseResources()
{
    midiPitch_.reset();
}

bool MetamorfoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const auto isMonoOrStereo = [] (const juce::AudioChannelSet& set) {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    if (! isMonoOrStereo (in) || ! isMonoOrStereo (out))
        return false;

    // Supported: mono->mono, mono->stereo, stereo->stereo. Not stereo->mono.
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void MetamorfoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    handleMidi (midi);

    // Hosts (FL Studio in particular) may deliver blocks larger than announced
    // in prepareToPlay; never trust the block size, process in chunks.
    const int numSamples = buffer.getNumSamples();
    const int chunkSize = std::max (1, preparedBlockSize_.load (std::memory_order_relaxed));

    for (int start = 0; start < numSamples; start += chunkSize)
        processChunk (buffer, start, std::min (chunkSize, numSamples - start));
}

void MetamorfoProcessor::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    // Phase 1: block-level handling (sample accuracy arrives with phase 4).
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            midiPitch_.noteOn (message.getNoteNumber(), message.getVelocity());
            midiActivity_.lastNote.store (message.getNoteNumber(), std::memory_order_relaxed);
            midiActivity_.lastVelocity.store (message.getVelocity(), std::memory_order_relaxed);
            midiActivity_.lastChannel.store (message.getChannel(), std::memory_order_relaxed);
        }
        else if (message.isNoteOff())
        {
            midiPitch_.noteOff (message.getNoteNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            midiPitch_.reset();
        }
        else
        {
            continue; // only note traffic counts as activity
        }

        midiActivity_.eventCount.fetch_add (1, std::memory_order_relaxed);
    }

    midiActivity_.heldNote.store (midiPitch_.activeNote(), std::memory_order_relaxed);
}

void MetamorfoProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Mono -> stereo: duplicate the analysed (mono) channel.
    const bool monoToStereo = numIn == 1 && numOut >= 2;
    if (monoToStereo)
        buffer.copyFrom (1, startSample, buffer, 0, startSample, numSamples);

    // Any other output channel without a source is cleared.
    for (int ch = monoToStereo ? 2 : numIn; ch < numOut; ++ch)
        buffer.clear (ch, startSample, numSamples);

    // Output sanitisation: never emit NaN/Inf.
    for (int ch = 0; ch < numOut; ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (data[i]))
                data[i] = 0.0f;
    }
}

juce::AudioProcessorEditor* MetamorfoProcessor::createEditor()
{
    return new ui::MetamorfoEditor (*this);
}

juce::String MetamorfoProcessor::getFormatName() const
{
    if (is_clap)
        return "CLAP";

    return juce::AudioProcessor::getWrapperTypeDescription (wrapperType);
}

void MetamorfoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters_.copyState();
    state.setProperty (kStateVersionProperty, kStateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MetamorfoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters_.state.getType()))
        return;

    // Future state versions migrate here, keyed on kStateVersionProperty.
    parameters_.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace metamorfo

// Entry point used by every JUCE plugin wrapper.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new metamorfo::MetamorfoProcessor();
}
