#include "plugin/PresetManager.h"

#include <cmath>

#include "plugin/Parameters.h"

namespace metamorfo
{
namespace
{
namespace id = params::id;

struct FactoryPreset
{
    const char* name;
    const char* category;
    float pitch, formant, link, mode, drive, mix, portamento;
};

// Original factory presets (SPEC §6). Mode: 0 Transponer, 1 Cuantizar, 2 Robot.
constexpr FactoryPreset kFactory[] = {
    //  name                               category        pitch formant link mode drive  mix  porta
    { "Inicial",                           "Inicial",       0.0f,  0.0f, 0.0f, 0.0f,  0.0f, 100.0f,  0.0f },
    { "Coros con cuerpo (−2 formante)",    "Coros",         0.0f, -2.0f, 0.0f, 0.0f,  0.0f, 100.0f,  0.0f },
    { "Doble octava abajo",                "Coros",       -12.0f,  0.0f, 0.0f, 0.0f,  1.0f,  50.0f,  0.0f },
    { "Octava arriba aireada",             "Coros",        12.0f, -1.5f, 0.0f, 0.0f,  1.5f,  40.0f,  0.0f },
    { "Tercera para armonía",              "Coros",         4.0f, -1.0f, 0.0f, 0.0f,  0.0f,  50.0f,  0.0f },
    { "Hard-tune urbano",                  "Hard-tune",     0.0f,  0.0f, 0.0f, 1.0f,  1.5f, 100.0f,  0.0f },
    { "Hard-tune con grit",                "Hard-tune",     0.0f,  0.0f, 0.0f, 1.0f,  7.0f, 100.0f,  0.0f },
    { "Melódico trap",                     "Hard-tune",     0.0f, -1.0f, 0.0f, 1.0f,  3.0f, 100.0f, 30.0f },
    { "Robot monótono",                    "Robot",         0.0f,  0.0f, 0.0f, 2.0f,  0.0f, 100.0f,  0.0f },
    { "Vocoder por MIDI",                  "Robot",         0.0f,  0.0f, 0.0f, 2.0f,  2.0f, 100.0f, 20.0f },
    { "Ardilla",                           "Personajes",   12.0f,  0.0f, 1.0f, 0.0f,  0.0f, 100.0f,  0.0f },
    { "Monstruo",                          "Personajes",  -12.0f, -8.0f, 0.0f, 0.0f,  6.0f, 100.0f,  0.0f },
    { "Voz de demonio",                    "Personajes",   -7.0f, -5.0f, 0.0f, 0.0f,  9.0f, 100.0f,  0.0f },
    { "Voz chiquita",                      "Personajes",    5.0f,  6.0f, 0.0f, 0.0f,  0.0f, 100.0f,  0.0f },
    { "Calidez sutil (drive)",             "Color",         0.0f,  0.0f, 0.0f, 0.0f,  2.5f, 100.0f,  0.0f },
    { "Voz rota",                          "Color",         0.0f,  2.0f, 0.0f, 0.0f, 10.0f,  80.0f,  0.0f },
    { "Radio distorsionado",               "Color",         0.0f,  4.0f, 0.0f, 0.0f, 12.0f, 100.0f,  0.0f },
};
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, ParameterLocks& locks)
    : state_ (state), locks_ (locks), userDirectory_ (defaultUserDirectory())
{
    refresh();
}

juce::File PresetManager::defaultUserDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Metamorfo").getChildFile ("Presets");
}

void PresetManager::setUserDirectory (const juce::File& directory)
{
    userDirectory_ = directory;
    refresh();
}

int PresetManager::numFactoryPresets()
{
    return static_cast<int> (std::size (kFactory));
}

PresetManager::Values PresetManager::factoryValues (int index)
{
    const auto& p = kFactory[index];
    return { { id::pitch, p.pitch }, { id::formant, p.formant }, { id::link, p.link }, { id::mode, p.mode },
             { id::drive, p.drive }, { id::mix, p.mix }, { id::portamento, p.portamento } };
}

void PresetManager::refresh()
{
    entries_.clear();
    for (int i = 0; i < numFactoryPresets(); ++i)
        entries_.push_back ({ juce::String::fromUTF8 (kFactory[i].name), juce::String::fromUTF8 (kFactory[i].category), true, i, {} });

    if (userDirectory_.isDirectory())
    {
        auto files = userDirectory_.findChildFiles (juce::File::findFiles, true, juce::String ("*") + kFileExtension);
        files.sort();
        for (const auto& f : files)
        {
            if (const auto xml = juce::XmlDocument::parse (f))
                entries_.push_back ({ xml->getStringAttribute ("name", f.getFileNameWithoutExtension()),
                                      xml->getStringAttribute ("category", kUserCategory), false, -1, f });
        }
    }
}

juce::StringArray PresetManager::categories() const
{
    juce::StringArray result;
    for (const auto& e : entries_)
        result.addIfNotAlreadyThere (e.category);
    return result;
}

PresetManager::Values PresetManager::currentValues() const
{
    Values values;
    for (auto* param : state_.processor.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (param))
            values[withId->paramID] = withId->convertFrom0to1 (withId->getValue());
    return values;
}

void PresetManager::applyValues (const Values& values)
{
    for (const auto& [paramId, plain] : values)
    {
        if (locks_.isLocked (paramId) || ! std::isfinite (plain)) // 'nan' / 'inf' in a file (B23)
            continue;
        if (auto* param = state_.getParameter (paramId))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (plain));
            param->endChangeGesture();
        }
    }
}

void PresetManager::setCurrentName (const juce::String& name)
{
    state_.state.setProperty (kPresetNameProperty, name, nullptr);
}

juce::String PresetManager::currentName() const
{
    return state_.state.getProperty (kPresetNameProperty, "Inicial").toString();
}

int PresetManager::currentIndex() const
{
    const auto name = currentName();
    for (int i = 0; i < static_cast<int> (entries_.size()); ++i)
        if (entries_[static_cast<std::size_t> (i)].name == name)
            return i;
    return -1;
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= static_cast<int> (entries_.size()))
        return false;

    const auto& e = entries_[static_cast<std::size_t> (index)];
    Values values;
    if (e.factory)
    {
        values = factoryValues (e.factoryIndex);
    }
    else
    {
        const auto xml = juce::XmlDocument::parse (e.file);
        if (xml == nullptr || ! xml->hasTagName ("MetamorfoPreset"))
            return false;
        for (auto* p : xml->getChildWithTagNameIterator ("Param"))
            values[p->getStringAttribute ("id")] = static_cast<float> (p->getDoubleAttribute ("value"));
    }

    applyValues (values);
    setCurrentName (e.name);
    return true;
}

bool PresetManager::loadByName (const juce::String& name)
{
    for (int i = 0; i < static_cast<int> (entries_.size()); ++i)
        if (entries_[static_cast<std::size_t> (i)].name == name)
            return load (i);
    return false;
}

void PresetManager::next()
{
    const int n = static_cast<int> (entries_.size());
    if (n > 0)
        load ((currentIndex() + 1 + n) % n);
}

void PresetManager::previous()
{
    const int n = static_cast<int> (entries_.size());
    if (n > 0)
        load ((std::max (0, currentIndex()) - 1 + n) % n);
}

namespace
{
// A single, harmless path component: legal characters, no separators, no leading or
// trailing dots/spaces (so never "." or ".."), bounded length. Empty -> fallback (B22).
juce::String safeComponent (const juce::String& text, const juce::String& fallback)
{
    auto legal = juce::File::createLegalFileName (text.trim())
                     .removeCharacters ("/\\")
                     .trimCharactersAtStart (". ")
                     .trimCharactersAtEnd (". ");
    if (legal.length() > 100)
        legal = legal.substring (0, 100).trimEnd();
    return legal.isEmpty() ? fallback : legal;
}
} // namespace

juce::File PresetManager::saveUserPreset (const juce::String& name, const juce::String& category)
{
    const auto safeName = safeComponent (name, "Preset");
    const auto safeCategory = safeComponent (category, kUserCategory);
    const auto folder = userDirectory_.getChildFile (safeCategory);
    if (! folder.isAChildOf (userDirectory_) || ! folder.createDirectory())
        return {};

    juce::XmlElement xml ("MetamorfoPreset");
    xml.setAttribute ("name", name.trim().isEmpty() ? safeName : name.trim());
    xml.setAttribute ("category", safeCategory);
    xml.setAttribute ("version", 1);
    for (const auto& [paramId, value] : currentValues())
    {
        if (paramId == id::midiReference) // a studio setting, not part of the sound
            continue;
        auto* p = xml.createNewChildElement ("Param");
        p->setAttribute ("id", paramId);
        p->setAttribute ("value", value);
    }

    auto file = folder.getChildFile (safeName + kFileExtension);
    if (! xml.writeTo (file)) // read-only folder, disk full...: report it to the caller
        return {};
    refresh();
    setCurrentName (xml.getStringAttribute ("name"));
    return file;
}

void PresetManager::toggleAB()
{
    abSlots_[static_cast<std::size_t> (activeSlot_)] = currentValues();
    activeSlot_ = 1 - activeSlot_;
    auto& other = abSlots_[static_cast<std::size_t> (activeSlot_)];
    if (other.has_value())
        applyValues (*other);
    else
        other = currentValues(); // first visit: B starts as a copy of A
}

void PresetManager::copyActiveToOther()
{
    abSlots_[static_cast<std::size_t> (1 - activeSlot_)] = currentValues();
}

} // namespace metamorfo
