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
    engineLatency_ = engine_.latencySamples();
    totalLatency_ = engineLatency_ + dsp::TubeDrive::kLatency;
    for (auto& d : drive_)
        d.prepare (sampleRate);

    std::uint32_t size = 1;
    while (size < static_cast<std::uint32_t> (totalLatency_ + maxBlock + 1))
        size <<= 1;
    for (auto& ring : dryDelay_)
        ring.assign (size, 0.0f);
    dryMask_ = size - 1;
    dryWrite_ = 0;

    midiDelay_ = pitchDetector_.maxPeriodSamples();
    midiQueueRead_ = midiQueueWrite_ = 0;
    streamPosition_ = 0;

    mixSmoothed_.reset (sampleRate, 0.02);
    mixSmoothed_.setCurrentAndTargetValue (mixParam_->load() / 100.0f);
    driveSmoothed_.reset (sampleRate, 0.03);
    driveSmoothed_.setCurrentAndTargetValue (driveParam_->load());
}

void MetamorfoProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedSampleRate_.store (sampleRate, std::memory_order_relaxed);
    preparedBlockSize_.store (std::max (1, maximumExpectedSamplesPerBlock), std::memory_order_relaxed);

    midiPitch_.reset();
    displayNotes_.reset();
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

    midiPitch_.setPortamento (portamentoParam_->load(), getSampleRate() > 0.0 ? getSampleRate() : 44100.0);

    // Hosts (FL Studio in particular) may deliver blocks larger than announced
    // in prepareToPlay; never trust the block size, process in chunks.
    const int numSamples = buffer.getNumSamples();
    const int chunkSize = std::max (1, preparedBlockSize_.load (std::memory_order_relaxed));

    for (int start = 0; start < numSamples; start += chunkSize)
        processChunk (buffer, start, std::min (chunkSize, numSamples - start), midi);

}

void MetamorfoProcessor::handleMidiMessage (const juce::MidiMessage& message) noexcept
{
    // Activity for the display: immediate.
    if (message.isNoteOn())
    {
        midiActivity_.lastNote.store (message.getNoteNumber(), std::memory_order_relaxed);
        midiActivity_.lastVelocity.store (message.getVelocity(), std::memory_order_relaxed);
        midiActivity_.lastChannel.store (message.getChannel(), std::memory_order_relaxed);
    }
    else if (! (message.isNoteOff() || message.isAllNotesOff() || message.isAllSoundOff()))
    {
        return; // only note traffic counts as activity
    }

    if (message.isNoteOn())
        displayNotes_.noteOn (message.getNoteNumber(), message.getVelocity());
    else if (message.isNoteOff())
        displayNotes_.noteOff (message.getNoteNumber());
    else
        displayNotes_.reset();

    midiActivity_.heldNote.store (displayNotes_.activeNote(), std::memory_order_relaxed);
    midiActivity_.eventCount.fetch_add (1, std::memory_order_relaxed);
}

void MetamorfoProcessor::applyMidiToEngine (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        midiPitch_.noteOn (message.getNoteNumber(), message.getVelocity());
    else if (message.isNoteOff())
        midiPitch_.noteOff (message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        midiPitch_.reset();
}

dsp::PsolaEngine::Controls MetamorfoProcessor::readControls (float midiNote) const noexcept
{
    dsp::PsolaEngine::Controls c;
    c.mode.mode = static_cast<dsp::ModeController::Mode> (std::clamp (static_cast<int> (std::lround (modeParam_->load())), 0, 2));
    c.mode.knobSemitones = pitchParam_->load();
    c.mode.midiNote = midiNote; // held (gliding) MIDI note, -1 = none -> PITCH knob
    c.mode.midiReference = static_cast<int> (std::lround (midiReferenceParam_->load()));
    c.formantSemitones = formantParam_->load();
    c.link = linkParam_->load() >= 0.5f;
    // Quantize and Robot retune "at maximum speed" (SPEC §4.2); Transpose glides softly.
    c.retuneMs = c.mode.mode == dsp::ModeController::Mode::transpose ? 20.0f : 1.0f;
    return c;
}

void MetamorfoProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                       const juce::MidiBuffer& midi) noexcept
{
    // 1) Queue the incoming events of this chunk (the last chunk also takes any event
    //    stamped beyond the block), due maxPeriod samples later.
    const int end = startSample + numSamples;
    const bool lastChunk = end >= buffer.getNumSamples();
    for (auto it = midi.findNextSamplePosition (startSample); it != midi.cend(); ++it)
    {
        const auto event = *it;
        if (event.samplePosition >= end && ! lastChunk)
            break;
        const auto message = event.getMessage();
        handleMidiMessage (message);
        if (message.getRawDataSize() > 3 || midiQueueWrite_ - midiQueueRead_ >= midiQueue_.size())
            continue; // sysex is irrelevant; a full queue drops (never blocks)
        auto& q = midiQueue_[midiQueueWrite_ % midiQueue_.size()];
        q.due = streamPosition_ + std::min (event.samplePosition, end - 1) - startSample + midiDelay_;
        q.size = message.getRawDataSize();
        std::copy_n (message.getRawData(), q.size, q.bytes);
        ++midiQueueWrite_;
    }

    // 2) Render, applying queued events sample-accurately when they fall due.
    int position = startSample;
    while (position < end)
    {
        int segmentEnd = end;
        if (midiQueueRead_ != midiQueueWrite_)
        {
            const auto& next = midiQueue_[midiQueueRead_ % midiQueue_.size()];
            const auto offset = next.due - (streamPosition_ + (position - startSample));
            if (offset <= 0)
            {
                applyMidiToEngine (juce::MidiMessage (next.bytes, next.size));
                ++midiQueueRead_;
                continue;
            }
            segmentEnd = static_cast<int> (std::min<std::int64_t> (end, position + offset));
        }
        renderSegment (buffer, position, segmentEnd - position);
        position = segmentEnd;
    }
    streamPosition_ += numSamples;

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

void MetamorfoProcessor::renderSegment (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    // Controls are refreshed every 32 samples so the portamento glide is smooth.
    constexpr int kControlBlock = 32;
    if (numSamples > kControlBlock)
    {
        for (int offset = 0; offset < numSamples; offset += kControlBlock)
            renderSegment (buffer, startSample + offset, std::min (kControlBlock, numSamples - offset));
        return;
    }

    const int numIn = getTotalNumInputChannels();
    const int numOut = std::min (getTotalNumOutputChannels(), 2);
    const auto controls = readControls (midiPitch_.advance (numSamples));
    mixSmoothed_.setTargetValue (mixParam_->load() / 100.0f);
    driveSmoothed_.setTargetValue (driveParam_->load());

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
        const float drive = driveSmoothed_.getNextValue();

        // Engine -> Drive -> Mix (SPEC §4). Two taps of the per-channel input delay:
        // `engineTap` aligns with the engine output, `dryTap` with the final output.
        const float inputs[2] = { l, r };
        const auto engineTap = (dryWrite_ - static_cast<std::uint32_t> (engineLatency_)) & dryMask_;
        const auto dryTap = (dryWrite_ - static_cast<std::uint32_t> (totalLatency_)) & dryMask_;
        for (int ch = 0; ch < numOut; ++ch)
        {
            auto& ring = dryDelay_[static_cast<std::size_t> (ch)];
            ring[dryWrite_] = inputs[ch];
            const float alignedInput = ring[engineTap];
            const float dry = ring[dryTap];
            // Neutral settings: the processed signal is the exactly delayed input (bit-exact).
            const float processed = identity >= 1.0f ? alignedInput : identity * alignedInput + (1.0f - identity) * wet;
            const float driven = drive_[static_cast<std::size_t> (ch)].process (processed, drive);
            out[ch][i] = dry + mix * (driven - dry);
        }
        dryWrite_ = (dryWrite_ + 1) & dryMask_;
    }

    // Any other output channel is cleared.
    for (int ch = numOut; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, startSample, numSamples);
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
