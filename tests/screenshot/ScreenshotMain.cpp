// MetamorfoScreenshot: renders the editor to PNG files (visual review, docs).
// Usage: MetamorfoScreenshot <output-dir>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace
{
void setPlain (metamorfo::MetamorfoProcessor& p, const char* id, float v)
{
    auto* param = p.getParameters().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

void shoot (metamorfo::MetamorfoProcessor& p, float scale, const juce::File& file)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
    editor->setSize (juce::roundToInt (780 * scale), juce::roundToInt (260 * scale));
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    p.editorBeingDeleted (editor.get());
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    namespace id = metamorfo::params::id;

    metamorfo::MetamorfoProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    shoot (p, 1.0f, dir.getChildFile ("ui_default_100.png"));

    setPlain (p, id::pitch, 7.0f);
    setPlain (p, id::link, 1.0f);
    setPlain (p, id::mode, 1.0f);
    setPlain (p, id::drive, 4.5f);
    setPlain (p, id::mix, 70.0f);
    p.getLocks().setLocked (id::mix, true);
    shoot (p, 1.5f, dir.getChildFile ("ui_link_quantize_lock_150.png"));
    return 0;
}
