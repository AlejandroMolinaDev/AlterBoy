#pragma once

#include <array>
#include <map>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/ParameterLocks.h"

namespace metamorfo
{

// Factory + user presets, A/B comparison (SPEC §6). Message thread only.
//
// A preset is a set of plain parameter values. Loading a preset (or switching A/B)
// skips locked parameters. User presets are XML files (*.mtmfpreset) in
// Documents/Metamorfo/Presets/ (sub-folder = category).
class PresetManager
{
public:
    using Values = std::map<juce::String, float>;

    struct Entry
    {
        juce::String name;
        juce::String category;
        bool factory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    static constexpr const char* kFileExtension = ".mtmfpreset";
    static constexpr const char* kPresetNameProperty = "presetName";
    static constexpr const char* kUserCategory = "Usuario";

    PresetManager (juce::AudioProcessorValueTreeState& state, ParameterLocks& locks);

    static juce::File defaultUserDirectory();
    void setUserDirectory (const juce::File& directory); // tests / custom location
    [[nodiscard]] const juce::File& userDirectory() const noexcept { return userDirectory_; }

    void refresh(); // rescans user presets
    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }
    [[nodiscard]] juce::StringArray categories() const;

    bool load (int index);
    bool loadByName (const juce::String& name);
    void next();
    void previous();
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] juce::String currentName() const;

    // Saves the current values as a user preset; returns the written file.
    juce::File saveUserPreset (const juce::String& name, const juce::String& category = kUserCategory);

    // A/B comparison.
    [[nodiscard]] int activeSlot() const noexcept { return activeSlot_; } // 0 = A, 1 = B
    void toggleAB();
    void copyActiveToOther();

    [[nodiscard]] Values currentValues() const;
    void applyValues (const Values& values); // respects locks

    static int numFactoryPresets();

private:
    void setCurrentName (const juce::String& name);
    static Values factoryValues (int index);

    juce::AudioProcessorValueTreeState& state_;
    ParameterLocks& locks_;
    juce::File userDirectory_;
    std::vector<Entry> entries_;
    std::array<std::optional<Values>, 2> abSlots_ {};
    int activeSlot_ = 0;
};

} // namespace metamorfo
