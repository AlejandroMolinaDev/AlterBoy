#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
    namespace id = params::id;
    pitchParam_ = parameters_.getRawParameterValue (id::pitch);
    formantParam_ = parameters_.getRawParameterValue (id::formant);
    linkParam_ = parameters_.getRawParameterValue (id::link);
    modeParam_ = parameters_.getRawParameterValue (id::mode);
    driveParam_ = parameters_.getRawParameterValue (id::drive);
    mixParam_ = parameters_.getRawParameterValue (id::mix);
    midiReferenceParam_ = parameters_.getRawParameterValue (id::midiReference);
    portamentoParam_ = parameters_.getRawParameterValue (id::portamento);

    // Some hosts call processBlock before prepareToPlay: start from a valid state.
    prepareEngine (44100.0, 512);
    setLatencySamples (totalLatency_);
}

void MetamorfoProcessor::prepareEngine (double sampleRate, int maxBlock)
{
    pitchDetector_.prepare (sampleRate);
    engine_.prepare (sampleRate, pitchDetector_.maxPeriodSamples());
    totalLatency_ = engine_.latencySamples();

    std::uint32_t size = 1;
    while (size < static_cast<std::uint32_t> (totalLatency_ + maxBlock + 1))
        size <<= 1;
    for (auto& ring : dryDelay_)
        ring.assign (size, 0.0f);
    dryMask_ = size - 1;
    dryWrite_ = 0;

    mixSmoothed_.reset (sampleRate, 0.02);
    mixSmoothed_.setCurrentAndTargetValue (mixParam_->load() / 100.0f);
}

void MetamorfoProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedSampleRate_.store (sampleRate, std::memory_order_relaxed);
    preparedBlockSize_.store (std::max (1, maximumExpectedSamplesPerBlock), std::memory_order_relaxed);

    midiPitch_.reset();
    midiActivity_.heldNote.store (-1, std::memory_order_relaxed);
    prepareEngine (sampleRate, std::max (1, maximumExpectedSamplesPerBlock));

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

dsp::PsolaEngine::Controls MetamorfoProcessor::readControls() const noexcept
{
    dsp::PsolaEngine::Controls c;
    c.mode.mode = static_cast<dsp::ModeController::Mode> (std::clamp (static_cast<int> (std::lround (modeParam_->load())), 0, 2));
    c.mode.knobSemitones = pitchParam_->load();
    c.mode.midiNote = -1.0f; // MIDI control of the engine arrives with phase 4
    c.mode.midiReference = static_cast<int> (std::lround (midiReferenceParam_->load()));
    c.formantSemitones = formantParam_->load();
    c.link = linkParam_->load() >= 0.5f;
    c.retuneMs = 20.0f;
    return c;
}

void MetamorfoProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const int numIn = getTotalNumInputChannels();
    const int numOut = std::min (getTotalNumOutputChannels(), 2);
    const auto controls = readControls();
    mixSmoothed_.setTargetValue (mixParam_->load() / 100.0f);

    float* out[2] = { buffer.getWritePointer (0, startSample),
                      numOut > 1 ? buffer.getWritePointer (1, startSample) : nullptr };
    const float* inL = buffer.getReadPointer (0, startSample);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1, startSample) : inL;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = std::isfinite (inL[i]) ? inL[i] : 0.0f;
        const float r = std::isfinite (inR[i]) ? inR[i] : 0.0f;

        // Analysis + engine on the mono sum (SPEC §2).
        const float mono = 0.5f * (l + r);
        pitchDetector_.push (mono);
        const float wet = engine_.processSample (mono, pitchDetector_, controls);
        const float identity = engine_.identityGain();
        const float mix = mixSmoothed_.getNextValue();

        // Dry path: each channel delayed by exactly the reported latency.
        const float inputs[2] = { l, r };
        const auto readIndex = (dryWrite_ - static_cast<std::uint32_t> (totalLatency_)) & dryMask_;
        for (int ch = 0; ch < numOut; ++ch)
        {
            dryDelay_[static_cast<std::size_t> (ch)][dryWrite_] = inputs[ch];
            const float dry = dryDelay_[static_cast<std::size_t> (ch)][readIndex];
            // Neutral settings: the "wet" signal is the exactly delayed dry (bit-exact).
            const float processed = identity >= 1.0f ? dry : identity * dry + (1.0f - identity) * wet;
            out[ch][i] = dry + mix * (processed - dry);
        }
        dryWrite_ = (dryWrite_ + 1) & dryMask_;
    }

    // Any other output channel is cleared.
    for (int ch = numOut; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, startSample, numSamples);

    // Display (read by the editor through atomics).
    const auto& estimate = pitchDetector_.latest();
    analysisDisplay_.inputF0.store (estimate.f0, std::memory_order_relaxed);
    const float outF0 = engine_.currentVoiced() ? engine_.currentInputF0() * engine_.currentRatio() : 0.0f;
    analysisDisplay_.outputF0.store (outF0, std::memory_order_relaxed);
    analysisDisplay_.confidence.store (1.0f - std::min (1.0f, estimate.aperiodicity), std::memory_order_relaxed);
    analysisDisplay_.voiced.store (estimate.voiced, std::memory_order_relaxed);

    // Output sanitisation: never emit NaN, Inf or denormals.
    for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (data[i]) || std::abs (data[i]) < std::numeric_limits<float>::min())
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
