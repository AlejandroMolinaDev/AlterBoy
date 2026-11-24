// Presets, A/B and parameter locks (SPEC §6, §7 "serialización y bloqueos").

#include <catch2/catch_test_macros.hpp>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using metamorfo::MetamorfoProcessor;
namespace id = metamorfo::params::id;

namespace
{
float plain (MetamorfoProcessor& p, const char* paramId) { return p.getParameters().getRawParameterValue (paramId)->load(); }

void setPlain (MetamorfoProcessor& p, const char* paramId, float v)
{
    auto* param = p.getParameters().getParameter (paramId);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("metamorfo-tests-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE ("Factory presets: at least 16, all categories of the SPEC, all load", "[presets]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    presets.setUserDirectory (tmp.dir);

    CHECK (metamorfo::PresetManager::numFactoryPresets() >= 17); // 16 of the SPEC + "Inicial"
    const auto categories = presets.categories();
    for (const char* c : { "Coros", "Hard-tune", "Robot", "Personajes", "Color" })
        CHECK (categories.contains (c));

    for (int i = 0; i < metamorfo::PresetManager::numFactoryPresets(); ++i)
    {
        REQUIRE (presets.load (i));
        CHECK (presets.currentIndex() == i);
    }

    REQUIRE (presets.loadByName (juce::String::fromUTF8 ("Robot monótono")));
    CHECK (plain (p, id::mode) == 2.0f);
    REQUIRE (presets.loadByName ("Ardilla"));
    CHECK (plain (p, id::pitch) == 12.0f);
    CHECK (plain (p, id::link) == 1.0f);
}

TEST_CASE ("Next / previous cycle through all presets", "[presets]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    presets.setUserDirectory (tmp.dir);
    presets.load (0);
    presets.next();
    CHECK (presets.currentIndex() == 1);
    presets.previous();
    presets.previous();
    CHECK (presets.currentIndex() == static_cast<int> (presets.entries().size()) - 1);
}

TEST_CASE ("User presets are saved as files and reload", "[presets]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    presets.setUserDirectory (tmp.dir);

    setPlain (p, id::pitch, -3.0f);
    setPlain (p, id::formant, 2.5f);
    setPlain (p, id::drive, 4.0f);
    const auto file = presets.saveUserPreset (juce::String::fromUTF8 ("Mi voz número 1"));
    REQUIRE (file.existsAsFile());
    CHECK (file.isAChildOf (tmp.dir));
    CHECK (presets.currentName() == juce::String::fromUTF8 ("Mi voz número 1"));

    presets.load (0); // back to "Inicial"
    CHECK (plain (p, id::pitch) == 0.0f);

    REQUIRE (presets.loadByName (juce::String::fromUTF8 ("Mi voz número 1")));
    CHECK (plain (p, id::pitch) == -3.0f);
    CHECK (plain (p, id::formant) == 2.5f);
    CHECK (plain (p, id::drive) == 4.0f);
}

TEST_CASE ("Locked parameters keep their value when a preset loads", "[presets][locks]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    presets.setUserDirectory (tmp.dir);

    setPlain (p, id::mix, 37.0f);
    p.getLocks().setLocked (id::mix, true);
    REQUIRE (presets.loadByName ("Monstruo"));
    CHECK (plain (p, id::mix) == 37.0f);     // locked: untouched
    CHECK (plain (p, id::pitch) == -12.0f);  // not locked: loaded

    p.getLocks().setLocked (id::mix, false);
    REQUIRE (presets.loadByName ("Monstruo"));
    CHECK (plain (p, id::mix) == 100.0f);
}

TEST_CASE ("Locks and preset name are saved in the project state", "[presets][locks][state]")
{
    MetamorfoProcessor a;
    TempDir tmp;
    a.getPresets().setUserDirectory (tmp.dir);
    a.getPresets().loadByName ("Voz chiquita");
    a.getLocks().setLocked (id::drive, true);
    a.getLocks().setLocked (id::formant, true);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    MetamorfoProcessor b;
    b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
    CHECK (b.getLocks().isLocked (id::drive));
    CHECK (b.getLocks().isLocked (id::formant));
    CHECK_FALSE (b.getLocks().isLocked (id::pitch));
    CHECK (b.getPresets().currentName() == "Voz chiquita");
    CHECK (plain (b, id::formant) == 6.0f);
}

TEST_CASE ("A/B comparison swaps between two settings", "[presets][ab]")
{
    MetamorfoProcessor p;
    auto& presets = p.getPresets();
    setPlain (p, id::pitch, 2.0f);
    CHECK (presets.activeSlot() == 0);

    presets.toggleAB(); // B starts as a copy of A
    CHECK (presets.activeSlot() == 1);
    CHECK (plain (p, id::pitch) == 2.0f);
    setPlain (p, id::pitch, -5.0f);

    presets.toggleAB(); // back to A
    CHECK (plain (p, id::pitch) == 2.0f);
    presets.toggleAB(); // B again
    CHECK (plain (p, id::pitch) == -5.0f);

    presets.copyActiveToOther(); // A := B
    presets.toggleAB();
    CHECK (plain (p, id::pitch) == -5.0f);
}
