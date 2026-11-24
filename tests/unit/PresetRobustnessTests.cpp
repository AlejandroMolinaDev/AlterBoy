// Robustness of value entry (B21) and of user presets on disk (B22): what users type
// and what they leave in the presets folder must never break or escape the plug-in.

#include <catch2/catch_test_macros.hpp>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using metamorfo::MetamorfoProcessor;
namespace id = metamorfo::params::id;

namespace
{
float plain (MetamorfoProcessor& p, const char* paramId) { return p.getParameters().getRawParameterValue (paramId)->load(); }

float parse (MetamorfoProcessor& p, const char* paramId, const char* text)
{
    auto* param = p.getParameters().getParameter (paramId);
    return param->convertFrom0to1 (param->getValueForText (juce::String::fromUTF8 (text)));
}

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("metamorfo-robust-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE ("Value entry accepts Spanish decimals, unicode minus and units (B21)", "[presets][parser]")
{
    MetamorfoProcessor p;
    CHECK (parse (p, id::pitch, "3,5") == 3.5f);
    CHECK (parse (p, id::pitch, "−3,5") == -3.5f); // U+2212, as shown on the UI scale
    CHECK (parse (p, id::pitch, "-12.0") == -12.0f);
    CHECK (parse (p, id::pitch, "+7 st") == 7.0f);
    CHECK (parse (p, id::pitch, " 2,5 st ") == 2.5f);
    CHECK (parse (p, id::pitch, "40") == 12.0f);      // clamped
    CHECK (parse (p, id::formant, "−2") == -2.0f);
    CHECK (parse (p, id::mix, "37,5 %") == 37.5f);
    CHECK (parse (p, id::drive, "4,25") == 4.25f);
    CHECK (parse (p, id::portamento, "120 ms") == 120.0f);
    CHECK (parse (p, id::midiReference, "C3 (48)") == 48.0f);
    CHECK (parse (p, id::midiReference, "55") == 55.0f);
}

TEST_CASE ("Value entry rejects text that is not a number (B21)", "[presets][parser]")
{
    float out = 0.0f;
    CHECK_FALSE (metamorfo::params::parseNumber (juce::String ("abc"), out));
    CHECK_FALSE (metamorfo::params::parseNumber (juce::String (""), out));
    REQUIRE (metamorfo::params::parseNumber (juce::String::fromUTF8 ("−1,25"), out));
    CHECK (out == -1.25f);
}

TEST_CASE ("Malformed, foreign or hostile preset files are ignored", "[presets][robustness]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    const auto user = tmp.dir.getChildFile ("Usuario");
    user.createDirectory();
    user.getChildFile ("roto.mtmfpreset").replaceWithText ("<MetamorfoPreset name='roto'><Param id=");
    user.getChildFile ("ajeno.mtmfpreset").replaceWithText ("<OtraCosa name='ajeno'/>");
    user.getChildFile ("raro.mtmfpreset").replaceWithText (
        "<MetamorfoPreset name='raro' category='Usuario'>"
        "<Param id='pitch' value='999'/><Param id='noExiste' value='3'/><Param id='formant' value='nan'/>"
        "<Param id='mix' value='-50'/></MetamorfoPreset>");
    presets.setUserDirectory (tmp.dir);

    for (const auto& e : presets.entries())
        CHECK (e.name != "roto"); // unparsable file not listed

    // A foreign XML is not loadable; the hostile one loads clamped values only.
    for (int i = 0; i < static_cast<int> (presets.entries().size()); ++i)
        if (presets.entries()[static_cast<std::size_t> (i)].name == "ajeno")
            CHECK_FALSE (presets.load (i));
    REQUIRE (presets.loadByName ("raro"));
    CHECK (plain (p, id::pitch) == 12.0f);
    CHECK (plain (p, id::mix) == 0.0f);
    CHECK (std::isfinite (plain (p, id::formant)));
}

TEST_CASE ("User preset names and categories cannot escape the presets folder (B22)", "[presets][robustness]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    presets.setUserDirectory (tmp.dir.getChildFile ("Presets"));

    for (const char* category : { "..", "../..", ".", "/etc", "C:\\Windows", "a/../../b", "" })
    {
        const auto file = presets.saveUserPreset ("prueba", juce::String::fromUTF8 (category));
        INFO ("category '" << category << "' -> " << file.getFullPathName());
        CHECK (file.existsAsFile());
        CHECK (file.isAChildOf (presets.userDirectory()));
    }
    for (const char* name : { "../fuera", "..", "a/b\\c", "", "   " })
    {
        const auto file = presets.saveUserPreset (juce::String::fromUTF8 (name));
        INFO ("name '" << name << "' -> " << file.getFullPathName());
        CHECK (file.isAChildOf (presets.userDirectory()));
    }
}

TEST_CASE ("Long unicode preset names round-trip", "[presets][robustness]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    presets.setUserDirectory (tmp.dir);
    const auto name = juce::String::fromUTF8 ("Voz ñandú «coros» 🎤 ") + juce::String::repeatedString ("larga ", 30);
    const auto file = presets.saveUserPreset (name);
    REQUIRE (file.existsAsFile());
    CHECK (file.getFileName().length() < 260);
    presets.load (0);
    REQUIRE (presets.loadByName (name.trim()));
}

TEST_CASE ("Saving a user preset reports a folder that cannot be written", "[presets][robustness]")
{
    MetamorfoProcessor p;
    TempDir tmp;
    auto& presets = p.getPresets();
    // The "presets folder" is an existing FILE: no sub-folder can be created (works as root too).
    const auto blocker = tmp.dir.getChildFile ("no-es-carpeta");
    blocker.replaceWithText ("x");
    presets.setUserDirectory (blocker);
    const auto file = presets.saveUserPreset ("prueba");
    CHECK_FALSE (file.existsAsFile());
    CHECK (file == juce::File());
}
