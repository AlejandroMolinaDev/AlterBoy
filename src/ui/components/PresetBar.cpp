#include "ui/components/PresetBar.h"

#include "plugin/Parameters.h"
#include "ui/MetamorfoLookAndFeel.h"

namespace metamorfo::ui
{
namespace
{
juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
} // namespace

PresetBar::PresetBar (PresetManager& presets, juce::AudioProcessorValueTreeState& state, MetamorfoLookAndFeel& lnf)
    : presets_ (presets), state_ (state), lnf_ (lnf)
{
    for (auto* b : { &previous_, &next_, &name_, &save_, &ab_, &copy_, &scale_, &options_ })
        addAndMakeVisible (*b);

    previous_.setTooltip ("Preset anterior");
    next_.setTooltip ("Preset siguiente");
    name_.setTooltip (utf8 ("Elegir preset por categoría"));
    save_.setTooltip ("Guardar los ajustes actuales como preset de usuario (Documentos/Metamorfo/Presets)");
    ab_.setTooltip (utf8 ("Comparación A/B: alterna entre dos ajustes"));
    copy_.setTooltip (utf8 ("Copia el ajuste activo al otro slot de la comparación A/B"));
    scale_.setTooltip (utf8 ("Tamaño de la interfaz (75–200 %). Se recuerda entre sesiones"));
    options_.setTooltip (utf8 ("Ayuda MIDI, nota de referencia MIDI, portamento y carpeta de presets"));

    previous_.onClick = [this] { presets_.previous(); refresh(); };
    next_.onClick = [this] { presets_.next(); refresh(); };
    name_.onClick = [this] { showPresetMenu(); };
    save_.onClick = [this] { promptSave(); };
    ab_.onClick = [this] { presets_.toggleAB(); refresh(); };
    copy_.onClick = [this] { presets_.copyActiveToOther(); };
    scale_.onClick = [this] { showScaleMenu(); };
    options_.onClick = [this] { showOptionsMenu(); };
    refresh();
}

void PresetBar::refresh()
{
    const auto name = presets_.currentName();
    if (name != shownName_)
    {
        shownName_ = name;
        name_.setButtonText (name + utf8 ("  ▾"));
    }
    if (presets_.activeSlot() != shownSlot_)
    {
        shownSlot_ = presets_.activeSlot();
        ab_.setButtonText (shownSlot_ == 0 ? "A" : "B");
        copy_.setButtonText (shownSlot_ == 0 ? utf8 ("A→B") : utf8 ("B→A"));
    }
    if (currentScale)
        scale_.setButtonText (juce::String (juce::roundToInt (currentScale() * 100.0f)) + " %");
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    previous_.setBounds (r.removeFromLeft (30));
    r.removeFromLeft (4);
    next_.setBounds (r.removeFromLeft (30));
    r.removeFromLeft (8);
    options_.setBounds (r.removeFromRight (84));
    r.removeFromRight (6);
    scale_.setBounds (r.removeFromRight (64));
    r.removeFromRight (14);
    copy_.setBounds (r.removeFromRight (48));
    r.removeFromRight (4);
    ab_.setBounds (r.removeFromRight (34));
    r.removeFromRight (8);
    save_.setBounds (r.removeFromRight (74));
    r.removeFromRight (8);
    name_.setBounds (r);
}

void PresetBar::paint (juce::Graphics&) {}

void PresetBar::showPresetMenu()
{
    presets_.refresh();
    juce::PopupMenu menu;
    const auto& entries = presets_.entries();
    const int current = presets_.currentIndex();
    for (const auto& category : presets_.categories())
    {
        juce::PopupMenu sub;
        for (int i = 0; i < static_cast<int> (entries.size()); ++i)
            if (entries[static_cast<std::size_t> (i)].category == category)
                sub.addItem (i + 1, entries[static_cast<std::size_t> (i)].name, true, i == current);
        menu.addSubMenu (category, sub);
    }
    menu.addSeparator();
    menu.addItem (10000, utf8 ("Guardar como…"));
    menu.addItem (10001, "Abrir la carpeta de presets");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name_),
                        [safe = juce::Component::SafePointer<PresetBar> (this)] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 10000)
                                safe->promptSave();
                            else if (result == 10001)
                            {
                                safe->presets_.userDirectory().createDirectory();
                                safe->presets_.userDirectory().startAsProcess();
                            }
                            else
                                safe->presets_.load (result - 1);
                            safe->refresh();
                        });
}

void PresetBar::showScaleMenu()
{
    juce::PopupMenu menu;
    const float current = currentScale ? currentScale() : 1.5f;
    for (const int pct : { 75, 100, 125, 150, 175, 200 })
        menu.addItem (pct, juce::String (pct) + " %", true, juce::roundToInt (current * 100.0f) == pct);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scale_),
                        [safe = juce::Component::SafePointer<PresetBar> (this)] (int result) {
                            if (safe != nullptr && result > 0 && safe->onScaleChosen)
                                safe->onScaleChosen (static_cast<float> (result) / 100.0f);
                        });
}

void PresetBar::showOptionsMenu()
{
    namespace id = params::id;
    auto* reference = state_.getParameter (id::midiReference);
    auto* portamento = state_.getParameter (id::portamento);
    const int refNote = juce::roundToInt (reference->convertFrom0to1 (reference->getValue()));
    const int portaMs = juce::roundToInt (portamento->convertFrom0to1 (portamento->getValue()));

    juce::PopupMenu refMenu;
    for (int note = 36; note <= 84; note += 1)
        if (note % 12 == 0 || note == refNote)
            refMenu.addItem (1000 + note, juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " (" + juce::String (note) + ")", true, note == refNote);
    juce::PopupMenu portaMenu;
    for (const int ms : { 0, 10, 25, 50, 100, 150, 200 })
        portaMenu.addItem (2000 + ms, juce::String (ms) + " ms", true, ms == portaMs);

    juce::PopupMenu menu;
    menu.addItem (1, "Control por MIDI en mi DAW");
    menu.addSeparator();
    menu.addSubMenu (utf8 ("Nota de referencia MIDI (Transponer / Cuantizar)"), refMenu);
    menu.addSubMenu ("Portamento entre notas MIDI", portaMenu);
    menu.addSeparator();
    menu.addItem (2, "Abrir la carpeta de presets");
    menu.addItem (3, utf8 ("Acerca de Metamorfo…"));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&options_),
                        [safe = juce::Component::SafePointer<PresetBar> (this), reference, portamento] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            const auto setPlain = [] (juce::RangedAudioParameter* p, float v) {
                                p->beginChangeGesture();
                                p->setValueNotifyingHost (p->convertTo0to1 (v));
                                p->endChangeGesture();
                            };
                            if (result == 1 && safe->onShowMidiHelp)
                                safe->onShowMidiHelp();
                            else if (result == 2)
                            {
                                safe->presets_.userDirectory().createDirectory();
                                safe->presets_.userDirectory().startAsProcess();
                            }
                            else if (result == 3)
                                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Metamorfo " METAMORFO_VERSION_STRING,
                                    utf8 ("Manipulador monofónico de tono y formantes vocales.\nMetamorfo Audio · Licencia AGPLv3.\n"
                                          "Fuentes Barlow Condensed y Share Tech Mono (SIL OFL)."));
                            else if (result >= 2000)
                                setPlain (portamento, static_cast<float> (result - 2000));
                            else if (result >= 1000)
                                setPlain (reference, static_cast<float> (result - 1000));
                        });
}

void PresetBar::promptSave()
{
    auto* window = new juce::AlertWindow ("Guardar preset", "Nombre del preset de usuario:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presets_.currentName());
    window->addTextEditor ("category", PresetManager::kUserCategory, utf8 ("Categoría"));
    window->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->setLookAndFeel (&lnf_);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetBar> (this), window] (int result) {
                                 if (safe == nullptr || result != 1)
                                     return;
                                 auto category = window->getTextEditorContents ("category").trim();
                                 const auto written = safe->presets_.saveUserPreset (window->getTextEditorContents ("name"),
                                                                                     category.isEmpty() ? juce::String (PresetManager::kUserCategory) : category);
                                 if (! written.existsAsFile())
                                     juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Guardar preset",
                                                                             utf8 ("No se pudo guardar el preset en:\n")
                                                                                 + safe->presets_.userDirectory().getFullPathName());
                                 safe->refresh();
                             }),
                             true);
}

// ---------------------------------------------------------------------------------------
MidiHelpScreen::MidiHelpScreen (MetamorfoLookAndFeel& lnf) : lnf_ (lnf)
{
    addAndMakeVisible (close_);
    close_.onClick = [this] { setVisible (false); };
    text_.setMultiLine (true);
    text_.setReadOnly (true);
    text_.setScrollbarsShown (true);
    text_.setCaretVisible (false);
    text_.setFont (lnf_.textFont (16.5f));
    text_.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    text_.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text_.setColour (juce::TextEditor::textColourId, palette::bone);
    text_.setText (utf8 (
        "FL STUDIO (Windows y macOS)\n"
        "1. Inserta Metamorfo en el canal del Mixer de la voz.\n"
        "2. En la ventana del plugin, engranaje del wrapper → Settings → MIDI → Input port = 1.\n"
        "3. En el Channel Rack: (+) → MIDI Out. En MIDI Out, Port = 1 (el mismo número).\n"
        "4. Toca o dibuja notas en el piano roll del canal MIDI Out.\n"
        "   · Robot: la voz canta exactamente esas notas.\n"
        "   · Transponer / Cuantizar: la nota define el intervalo respecto a la nota de referencia (Opciones).\n"
        "   · Sin notas, Metamorfo vuelve a la perilla PITCH.\n\n"
        "ABLETON LIVE\n"
        "Crea una pista MIDI → MIDI To: la pista de la voz → \"Metamorfo\". Monitor en \"In\".\n\n"
        "LOGIC PRO (AU)\n"
        "Inserta Metamorfo como Audio FX y elige una pista de instrumento en el menú \"Side Chain MIDI\" del plugin.\n\n"
        "REAPER\n"
        "Pista MIDI con un envío (Send) MIDI hacia la pista de la voz; Metamorfo recibe ese MIDI.\n\n"
        "BITWIG STUDIO\n"
        "Pista de notas → salida de notas hacia la pista de la voz (Note Receiver) o usa el puerto de notas CLAP.\n\n"
        "STUDIO ONE / CUBASE\n"
        "Pista de instrumento o MIDI con su salida dirigida a Metamorfo (si el host lo ofrece para efectos VST3).\n\n"
        "Detalles y estado por DAW: docs/DAW_MATRIX.md."));
    addAndMakeVisible (text_);
}

void MidiHelpScreen::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto r = getLocalBounds().toFloat().reduced (24.0f, 14.0f);
    g.setColour (juce::Colour (0xf2232819));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (palette::boneDim.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 8.0f, 1.2f);
    MetamorfoLookAndFeel::drawEngravedText (g, "CONTROL POR MIDI EN MI DAW", r.reduced (16.0f, 8.0f).removeFromTop (26.0f),
                                            lnf_.labelFont (22.0f), juce::Justification::centredLeft, palette::phosphor);
}

void MidiHelpScreen::resized()
{
    auto r = getLocalBounds().reduced (24, 14).reduced (16, 8);
    auto header = r.removeFromTop (26);
    close_.setBounds (header.removeFromRight (100));
    r.removeFromTop (6);
    text_.setBounds (r);
}

void MidiHelpScreen::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().reduced (24, 14).contains (e.getPosition()))
        setVisible (false);
}

} // namespace metamorfo::ui
