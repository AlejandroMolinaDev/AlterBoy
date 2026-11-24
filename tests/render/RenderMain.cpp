// MetamorfoRender: offline host for the plugin processor. Renders a WAV file
// through MetamorfoProcessor with given parameters, simulated MIDI notes and
// block-size patterns, and writes a JSON report (latency, CPU, NaN/Inf/denormals).
//
// Usage:
//   MetamorfoRender --in in.wav --out out.wav [options]
// Options:
//   --layout mono|stereo|mono-stereo   bus layout (default stereo)
//   --block N                          fixed block size (default 512)
//   --blocks irregular                 pseudo-random block sizes 1..4096 (prepared 512)
//   --prepared N                       size announced in prepareToPlay (default = block)
//   --param id=value                   plain parameter value (repeatable)
//   --param-at seconds:id=value        parameter change during playback (repeatable)
//   --note start:duration:note:vel     MIDI note, times in seconds (repeatable)
//   --repeat N                         process the input N times (CPU benchmark)
//   --report path.json                 write a JSON report
//   --preset "name"                    load a factory/user preset first (then --param overrides)
//   --list-presets                     print the factory preset names (one per line) and exit

#include <atomic>
#include <cfloat>
#include <cstdlib>
#include <new>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "plugin/PluginProcessor.h"

// ---------------------------------------------------------------------------
// Allocation probe: counts heap allocations made while processBlock() runs.
// Real-time rule (SPEC §4): this must stay at zero.
// ---------------------------------------------------------------------------
namespace
{
thread_local bool insideProcessBlock = false;
std::atomic<std::int64_t> allocationsInProcess { 0 };
std::atomic<std::int64_t> allocationsTotal { 0 }; // proves the probe is active
} // namespace

void* operator new (std::size_t size)
{
    allocationsTotal.fetch_add (1, std::memory_order_relaxed);
    if (insideProcessBlock)
        allocationsInProcess.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
struct ParamChange
{
    double time = 0.0;
    juce::String id;
    float value = 0.0f;
};

struct Note
{
    double start = 0.0;
    double duration = 0.0;
    int note = 60;
    int velocity = 100;
};

struct Options
{
    juce::File input, output, report;
    juce::String layout = "stereo";
    juce::String preset;
    bool listPresets = false;
    int block = 512;
    int prepared = -1;
    bool irregular = false;
    int repeat = 1;
    std::vector<ParamChange> initialParams;
    std::vector<ParamChange> timedParams;
    std::vector<Note> notes;
};

[[noreturn]] void fail (const juce::String& message)
{
    std::cerr << "MetamorfoRender: " << message << std::endl;
    std::exit (2);
}

ParamChange parseParam (const juce::String& text, bool timed)
{
    ParamChange change;
    auto rest = text;
    if (timed)
    {
        change.time = rest.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
        rest = rest.fromFirstOccurrenceOf (":", false, false);
    }
    change.id = rest.upToFirstOccurrenceOf ("=", false, false).trim();
    change.value = rest.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    if (change.id.isEmpty())
        fail ("bad parameter spec: " + text);
    return change;
}

Options parseArgs (const juce::StringArray& args)
{
    Options o;
    for (int i = 1; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&] { if (i + 1 >= args.size()) fail ("missing value for " + a); return args[++i]; };

        if (a == "--in") o.input = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--out") o.output = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--report") o.report = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--layout") o.layout = next();
        else if (a == "--block") o.block = next().getIntValue();
        else if (a == "--blocks") o.irregular = next() == "irregular";
        else if (a == "--prepared") o.prepared = next().getIntValue();
        else if (a == "--repeat") o.repeat = std::max (1, next().getIntValue());
        else if (a == "--param") o.initialParams.push_back (parseParam (next(), false));
        else if (a == "--preset") o.preset = next();
        else if (a == "--list-presets") o.listPresets = true;
        else if (a == "--param-at") o.timedParams.push_back (parseParam (next(), true));
        else if (a == "--note")
        {
            juce::StringArray parts;
            parts.addTokens (next(), ":", {});
            if (parts.size() != 4) fail ("bad note spec, expected start:duration:note:velocity");
            o.notes.push_back ({ parts[0].getDoubleValue(), parts[1].getDoubleValue(),
                                 parts[2].getIntValue(), parts[3].getIntValue() });
        }
        else fail ("unknown argument " + a);
    }

    if (o.listPresets) return o;
    if (! o.input.existsAsFile()) fail ("input file not found");
    if (o.block < 1 || o.block > 65536) fail ("invalid block size");
    if (o.prepared < 0) o.prepared = o.irregular ? 512 : o.block;
    return o;
}

void setParam (metamorfo::MetamorfoProcessor& p, const ParamChange& change)
{
    auto* param = p.getParameters().getParameter (change.id);
    if (param == nullptr)
        fail ("unknown parameter id " + change.id);
    param->setValueNotifyingHost (param->convertTo0to1 (change.value));
}

int run (const Options& o)
{
    if (o.listPresets)
    {
        metamorfo::MetamorfoProcessor p;
        for (const auto& e : p.getPresets().entries())
            if (e.factory)
                std::cout << e.name << std::endl;
        return 0;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (o.input));
    if (reader == nullptr) fail ("cannot read " + o.input.getFullPathName());

    const double sampleRate = reader->sampleRate;
    const int numSamples = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> input (static_cast<int> (reader->numChannels), numSamples);
    reader->read (&input, 0, numSamples, 0, true, true);

    const auto inSet = o.layout == "stereo" ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
    const auto outSet = o.layout == "mono" ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();

    metamorfo::MetamorfoProcessor processor;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (inSet);
    layout.outputBuses.add (outSet);
    if (! processor.setBusesLayout (layout)) fail ("layout not supported: " + o.layout);

    if (o.preset.isNotEmpty() && ! processor.getPresets().loadByName (o.preset))
        fail ("unknown preset " + o.preset);
    for (const auto& change : o.initialParams)
        setParam (processor, change);

    processor.setRateAndBufferSizeDetails (sampleRate, o.prepared);
    processor.prepareToPlay (sampleRate, o.prepared);

    const int numIn = inSet.size();
    const int numOut = outSet.size();
    const int bufferChannels = std::max (numIn, numOut);

    juce::AudioBuffer<float> output (numOut, numSamples);
    std::vector<int> blockSizes;
    std::mt19937 rng (12345u);
    std::uniform_int_distribution<int> sizeDist (1, 4096);

    // Pre-allocate the largest block so the timed loop does not allocate.
    juce::AudioBuffer<float> block (bufferChannels, o.irregular ? 4096 : o.block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    std::int64_t nanCount = 0, infCount = 0, denormalCount = 0;
    std::vector<double> detectedF0, outputF0;
    int analysisPolls = 0, voicedPolls = 0;
    double peak = 0.0;
    double processSeconds = 0.0;
    int numBlocks = 0, maxBlock = 0;

    for (int pass = 0; pass < o.repeat; ++pass)
    {
        auto timed = o.timedParams;
        std::size_t nextTimed = 0;
        std::sort (timed.begin(), timed.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });

        for (int position = 0; position < numSamples;)
        {
            const int size = std::min (o.irregular ? sizeDist (rng) : o.block, numSamples - position);
            const double blockStart = position / sampleRate;
            const double blockEnd = (position + size) / sampleRate;

            while (nextTimed < timed.size() && timed[nextTimed].time < blockEnd)
                setParam (processor, timed[nextTimed++]);

            midi.clear();
            for (const auto& n : o.notes)
            {
                const double end = n.start + n.duration;
                if (n.start >= blockStart && n.start < blockEnd)
                    midi.addEvent (juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity)),
                                   static_cast<int> ((n.start - blockStart) * sampleRate));
                if (end >= blockStart && end < blockEnd)
                    midi.addEvent (juce::MidiMessage::noteOff (1, n.note),
                                   static_cast<int> ((end - blockStart) * sampleRate));
            }

            block.setSize (bufferChannels, size, false, false, true);
            for (int ch = 0; ch < bufferChannels; ++ch)
            {
                if (ch < numIn)
                    block.copyFrom (ch, 0, input, std::min (ch, input.getNumChannels() - 1), position, size);
                else
                    block.clear (ch, 0, size);
            }

            const auto t0 = juce::Time::getHighResolutionTicks();
            insideProcessBlock = true;
            processor.processBlock (block, midi);
            insideProcessBlock = false;
            processSeconds += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);

            if (pass == o.repeat - 1)
            {
                // Poll the analysis display like the editor does (G3: detector is measurable).
                const auto& analysis = processor.getAnalysisDisplay();
                ++analysisPolls;
                if (analysis.voiced.load())
                {
                    ++voicedPolls;
                    if (analysis.inputF0.load() > 0.0f) detectedF0.push_back (analysis.inputF0.load());
                    if (analysis.outputF0.load() > 0.0f) outputF0.push_back (analysis.outputF0.load());
                }

                for (int ch = 0; ch < numOut; ++ch)
                {
                    const float* data = block.getReadPointer (ch);
                    for (int i = 0; i < size; ++i)
                    {
                        const float x = data[i];
                        if (std::isnan (x)) ++nanCount;
                        else if (std::isinf (x)) ++infCount;
                        else if (x != 0.0f && std::abs (x) < FLT_MIN) ++denormalCount;
                        else peak = std::max (peak, static_cast<double> (std::abs (x)));
                    }
                    output.copyFrom (ch, position, block, ch, 0, size);
                }
                blockSizes.push_back (size);
                maxBlock = std::max (maxBlock, size);
            }

            ++numBlocks;
            position += size;
        }
    }

    processor.releaseResources();

    if (o.output != juce::File())
    {
        o.output.getParentDirectory().createDirectory();
        o.output.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (o.output.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions {}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (numOut)
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr) fail ("cannot write " + o.output.getFullPathName());
        writer->writeFromAudioSampleBuffer (output, 0, numSamples);
    }

    const double audioSeconds = static_cast<double> (numSamples) * o.repeat / sampleRate;

    auto* json = new juce::DynamicObject();
    json->setProperty ("sample_rate", sampleRate);
    json->setProperty ("num_samples", numSamples);
    json->setProperty ("layout", o.layout);
    json->setProperty ("prepared_block", o.prepared);
    json->setProperty ("irregular_blocks", o.irregular);
    json->setProperty ("num_blocks", numBlocks);
    json->setProperty ("max_block", maxBlock);
    json->setProperty ("reported_latency_samples", processor.getLatencySamples());
    json->setProperty ("nan_count", static_cast<juce::int64> (nanCount));
    json->setProperty ("inf_count", static_cast<juce::int64> (infCount));
    json->setProperty ("denormal_count", static_cast<juce::int64> (denormalCount));
    json->setProperty ("peak", peak);
    json->setProperty ("allocations_in_process", static_cast<juce::int64> (allocationsInProcess.load()));
    json->setProperty ("allocations_total", static_cast<juce::int64> (allocationsTotal.load()));
    json->setProperty ("process_seconds", processSeconds);
    json->setProperty ("audio_seconds", audioSeconds);
    json->setProperty ("cpu_percent_of_core", 100.0 * processSeconds / audioSeconds);
    const auto median = [] (std::vector<double> v) {
        if (v.empty()) return 0.0;
        std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    json->setProperty ("detected_f0_median", median (detectedF0));
    json->setProperty ("output_f0_median", median (outputF0));
    json->setProperty ("voiced_ratio", analysisPolls > 0 ? static_cast<double> (voicedPolls) / analysisPolls : 0.0);
    const juce::var report (json);

    if (o.report != juce::File())
        o.report.replaceWithText (juce::JSON::toString (report));
    else
        std::cout << juce::JSON::toString (report) << std::endl;

    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 0; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));
    return run (parseArgs (args));
}
