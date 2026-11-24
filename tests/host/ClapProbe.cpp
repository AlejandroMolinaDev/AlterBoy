// Minimal CLAP host used by MetamorfoHostProbe. It checks what FL Studio needs to
// route a "MIDI Out" channel into the CLAP effect: a note input port with the MIDI
// dialect. It also activates, processes notes and round-trips the state.

#include <cmath>
#include <cstring>
#include <vector>

#include <clap/clap.h>
#include <juce_core/juce_core.h>

#include "ProbeCheck.h"

namespace probe
{
namespace
{
// ---- host ------------------------------------------------------------------
const void* hostGetExtension (const clap_host_t*, const char*) { return nullptr; }
void hostNoop (const clap_host_t*) {}

clap_host_t makeHost()
{
    clap_host_t host {};
    host.clap_version = CLAP_VERSION;
    host.host_data = nullptr;
    host.name = "MetamorfoHostProbe";
    host.vendor = "Metamorfo Audio";
    host.url = "";
    host.version = "1.0";
    host.get_extension = hostGetExtension;
    host.request_restart = hostNoop;
    host.request_process = hostNoop;
    host.request_callback = hostNoop;
    return host;
}

// ---- event lists -------------------------------------------------------------
struct InputEvents
{
    std::vector<std::vector<std::uint8_t>> storage;
    clap_input_events_t list {};

    InputEvents()
    {
        list.ctx = this;
        list.size = [] (const clap_input_events_t* l) -> std::uint32_t {
            return static_cast<std::uint32_t> (static_cast<const InputEvents*> (l->ctx)->storage.size());
        };
        list.get = [] (const clap_input_events_t* l, std::uint32_t i) -> const clap_event_header_t* {
            return reinterpret_cast<const clap_event_header_t*> (static_cast<const InputEvents*> (l->ctx)->storage[i].data());
        };
    }

    template <typename Event>
    void add (const Event& e)
    {
        std::vector<std::uint8_t> bytes (sizeof (Event));
        std::memcpy (bytes.data(), &e, sizeof (Event));
        storage.push_back (std::move (bytes));
    }
};

clap_event_note_t noteEvent (std::uint16_t type, std::uint32_t time, std::int16_t key, double velocity)
{
    clap_event_note_t e {};
    e.header.size = sizeof (e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = type;
    e.note_id = -1;
    e.port_index = 0;
    e.channel = 0;
    e.key = key;
    e.velocity = velocity;
    return e;
}

clap_event_midi_t midiEvent (std::uint32_t time, std::uint8_t status, std::uint8_t d1, std::uint8_t d2)
{
    clap_event_midi_t e {};
    e.header.size = sizeof (e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_MIDI;
    e.port_index = 0;
    e.data[0] = status;
    e.data[1] = d1;
    e.data[2] = d2;
    return e;
}

bool outputTryPush (const clap_output_events_t*, const clap_event_header_t*) { return true; }

// ---- state streams -------------------------------------------------------------
struct OutStream
{
    std::vector<std::uint8_t> bytes;
    clap_ostream_t stream {};
    OutStream()
    {
        stream.ctx = this;
        stream.write = [] (const clap_ostream_t* s, const void* buffer, std::uint64_t size) -> std::int64_t {
            auto& b = static_cast<OutStream*> (s->ctx)->bytes;
            const auto* p = static_cast<const std::uint8_t*> (buffer);
            b.insert (b.end(), p, p + size);
            return static_cast<std::int64_t> (size);
        };
    }
};

struct InStream
{
    const std::vector<std::uint8_t>& bytes;
    std::size_t position = 0;
    clap_istream_t stream {};
    explicit InStream (const std::vector<std::uint8_t>& b) : bytes (b)
    {
        stream.ctx = this;
        stream.read = [] (const clap_istream_t* s, void* buffer, std::uint64_t size) -> std::int64_t {
            auto* self = static_cast<InStream*> (s->ctx);
            const auto n = std::min<std::uint64_t> (size, self->bytes.size() - self->position);
            std::memcpy (buffer, self->bytes.data() + self->position, n);
            self->position += n;
            return static_cast<std::int64_t> (n);
        };
    }
};

juce::File binaryFor (const juce::File& clapPath)
{
#if JUCE_MAC
    return clapPath.getChildFile ("Contents/MacOS").getChildFile (clapPath.getFileNameWithoutExtension());
#else
    return clapPath;
#endif
}

bool hasFeature (const clap_plugin_descriptor_t* d, const char* feature)
{
    for (const char* const* f = d->features; f != nullptr && *f != nullptr; ++f)
        if (std::strcmp (*f, feature) == 0)
            return true;
    return false;
}
} // namespace

int runClapProbe (const std::string& path)
{
    std::cout << "CLAP probe: " << path << std::endl;
    const juce::File clapPath (path);
    juce::DynamicLibrary library;
    if (! check (library.open (binaryFor (clapPath).getFullPathName()), "binary loads (dlopen/LoadLibrary)"))
        return 1;

    const auto* entry = static_cast<const clap_plugin_entry_t*> (library.getFunction ("clap_entry"));
    if (! check (entry != nullptr, "exports clap_entry"))
        return 1;
    if (! check (entry->init (clapPath.getFullPathName().toRawUTF8()), "entry init"))
        return 1;

    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (! check (factory != nullptr && factory->get_plugin_count (factory) == 1, "factory exposes one plugin"))
        return 1;

    const auto* desc = factory->get_plugin_descriptor (factory, 0);
    check (std::strcmp (desc->id, "com.metamorfoaudio.metamorfo") == 0, std::string ("plugin id ") + desc->id);
    check (std::strcmp (desc->name, "Metamorfo") == 0, std::string ("name ") + desc->name);
    check (std::strcmp (desc->vendor, "Metamorfo Audio") == 0, std::string ("vendor ") + desc->vendor);
    check (hasFeature (desc, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT), "feature audio-effect");
    check (! hasFeature (desc, CLAP_PLUGIN_FEATURE_INSTRUMENT), "not an instrument");

    const auto host = makeHost();
    const auto* plugin = factory->create_plugin (factory, &host, desc->id);
    if (! check (plugin != nullptr && plugin->init (plugin), "plugin created and initialised"))
        return 1;

    // Note input port with the MIDI dialect: what FL's MIDI Out -> port routing needs.
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS));
    if (check (notePorts != nullptr, "implements clap.note-ports"))
    {
        check (notePorts->count (plugin, true) == 1, "one note INPUT port");
        check (notePorts->count (plugin, false) == 0, "no note output port");
        clap_note_port_info_t info {};
        if (check (notePorts->get (plugin, 0, true, &info), "note input port info"))
            check ((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0, "note input port supports the MIDI dialect");
    }

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
    if (check (audioPorts != nullptr, "implements clap.audio-ports"))
    {
        check (audioPorts->count (plugin, true) == 1 && audioPorts->count (plugin, false) == 1, "one audio input and one output port");
        clap_audio_port_info_t in {}, out {};
        audioPorts->get (plugin, 0, true, &in);
        audioPorts->get (plugin, 0, false, &out);
        check (in.channel_count == 2 && out.channel_count == 2, "default ports are stereo");
    }

    constexpr std::uint32_t frames = 256;
    check (plugin->activate (plugin, 48000.0, 1, frames), "activate at 48 kHz");
    check (plugin->start_processing (plugin), "start processing");

    std::vector<float> inL (frames), inR (frames), outL (frames), outR (frames);
    float* inPtrs[2] = { inL.data(), inR.data() };
    float* outPtrs[2] = { outL.data(), outR.data() };
    clap_audio_buffer_t inBuffer {}, outBuffer {};
    inBuffer.data32 = inPtrs;
    inBuffer.channel_count = 2;
    outBuffer.data32 = outPtrs;
    outBuffer.channel_count = 2;
    clap_output_events_t outEvents { nullptr, outputTryPush };

    bool processedOk = true;
    float maxError = 0.0f;
    for (int block = 0; block < 50; ++block)
    {
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            inL[i] = 0.4f * std::sin (0.02f * static_cast<float> (block * frames + i));
            inR[i] = 0.3f * std::sin (0.03f * static_cast<float> (block * frames + i));
        }
        InputEvents events;
        if (block % 10 == 0) events.add (noteEvent (CLAP_EVENT_NOTE_ON, 10, static_cast<std::int16_t> (60 + block / 10), 0.8));
        if (block % 10 == 5) events.add (noteEvent (CLAP_EVENT_NOTE_OFF, 0, static_cast<std::int16_t> (60 + block / 10), 0.0));
        if (block == 7) events.add (midiEvent (5, 0x90, 72, 100)); // raw MIDI dialect
        if (block == 8) events.add (midiEvent (5, 0x80, 72, 0));

        clap_process_t process {};
        process.steady_time = static_cast<std::int64_t> (block) * frames;
        process.frames_count = frames;
        process.audio_inputs = &inBuffer;
        process.audio_inputs_count = 1;
        process.audio_outputs = &outBuffer;
        process.audio_outputs_count = 1;
        process.in_events = &events.list;
        process.out_events = &outEvents;

        processedOk &= plugin->process (plugin, &process) != CLAP_PROCESS_ERROR;
        for (std::uint32_t i = 0; i < frames; ++i)
            maxError = std::max ({ maxError, std::abs (outL[i] - inL[i]), std::abs (outR[i] - inR[i]) });
    }
    check (processedOk, "process() with note + MIDI events never returns an error");
    check (maxError < 1.0e-6f, "passthrough through the binary (max error " + std::to_string (maxError) + ")");

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);

    // State round trip: save, load into a new instance, save again -> identical bytes.
    const auto* state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
    if (check (state != nullptr, "implements clap.state"))
    {
        OutStream first;
        check (state->save (plugin, &first.stream) && ! first.bytes.empty(), "state saved");

        const auto* other = factory->create_plugin (factory, &host, desc->id);
        if (check (other != nullptr && other->init (other), "second instance created"))
        {
            const auto* otherState = static_cast<const clap_plugin_state_t*> (other->get_extension (other, CLAP_EXT_STATE));
            InStream in (first.bytes);
            check (otherState->load (other, &in.stream), "state loaded into the new instance");
            OutStream second;
            otherState->save (other, &second.stream);
            check (second.bytes == first.bytes, "re-saved state is identical");
            other->destroy (other);
        }
    }

    plugin->destroy (plugin);
    entry->deinit();
    library.close();
    return failures() == 0 ? 0 : 1;
}
} // namespace probe
