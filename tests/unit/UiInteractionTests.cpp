// UI interaction with synthetic mouse events on the real editor (SPEC §6 knob
// utilities, lever, LINK, preset bar, UI scale) and text-fit checks.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"
#include "ui/components/Knob.h"
#include "ui/components/ToggleLever3.h"

#include "TestHelpers.h"

using metamorfo::MetamorfoProcessor;
namespace id = metamorfo::params::id;

namespace
{
bool hasDisplay()
{
#if JUCE_LINUX
    return std::getenv ("DISPLAY") != nullptr;
#else
    return true;
#endif
}

float plain (MetamorfoProcessor& p, const char* paramId) { return p.getParameters().getRawParameterValue (paramId)->load(); }

void setPlain (MetamorfoProcessor& p, const char* paramId, float v)
{
    auto* param = p.getParameters().getParameter (paramId);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

template <typename T>
void collect (juce::Component& root, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&root))
        out.push_back (t);
    for (auto* child : root.getChildren())
        collect (*child, out);
}

template <typename T>
std::vector<T*> findAll (juce::Component& root)
{
    std::vector<T*> out;
    collect (root, out);
    return out;
}

metamorfo::ui::Knob* findKnob (juce::Component& root, const char* paramId)
{
    for (auto* k : findAll<metamorfo::ui::Knob> (root))
        if (k->parameterId() == paramId)
            return k;
    return nullptr;
}

juce::TextButton* findButton (juce::Component& root, const juce::String& textStart)
{
    for (auto* b : findAll<juce::TextButton> (root))
        if (b->getButtonText().startsWith (textStart))
            return b;
    return nullptr;
}

juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods = {}, int clicks = 1,
                          juce::Point<float> downPos = {}, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                             juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, now, dragged ? downPos : pos, now, clicks, dragged);
}

// Owns an editor and follows the JUCE host protocol on destruction.
struct EditorFixture
{
    MetamorfoProcessor& p;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    explicit EditorFixture (MetamorfoProcessor& proc) : p (proc), editor (proc.createEditorAndMakeActive())
    {
        REQUIRE (editor != nullptr);
        editor->setSize (780, 260); // 100 %: base coordinates
    }
    ~EditorFixture()
    {
        p.editorBeingDeleted (editor.get());
        editor.reset();
    }
};
} // namespace

TEST_CASE ("Knob: double-click returns to the default value", "[ui][knob]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    setPlain (p, id::pitch, 5.0f);
    EditorFixture f (p);
    auto* knob = findKnob (*f.editor, id::pitch);
    REQUIRE (knob != nullptr);
    auto* slider = dynamic_cast<juce::Slider*> (knob->getChildComponent (0));
    REQUIRE (slider != nullptr);
    const auto centre = slider->getLocalBounds().getCentre().toFloat();
    slider->mouseDoubleClick (mouseAt (*slider, centre, {}, 2));
    CHECK (plain (p, id::pitch) == 0.0f);
}

TEST_CASE ("Knob: drag adjusts, Shift/Cmd drag is finer", "[ui][knob]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    const auto dragDelta = [] (juce::ModifierKeys mods) {
        MetamorfoProcessor p;
        EditorFixture f (p);
        auto* slider = dynamic_cast<juce::Slider*> (findKnob (*f.editor, id::drive)->getChildComponent (0));
        const auto start = slider->getLocalBounds().getCentre().toFloat();
        const auto end = start.translated (0.0f, -40.0f);
        slider->mouseDown (mouseAt (*slider, start, mods));
        slider->mouseDrag (mouseAt (*slider, end, mods, 1, start, true));
        slider->mouseUp (mouseAt (*slider, end, mods, 1, start, true));
        return plain (p, id::drive);
    };
    const float coarse = dragDelta ({});
    const float fine = dragDelta (juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
    INFO ("coarse " << coarse << " fine " << fine);
    CHECK (coarse > 0.5f);
    CHECK (fine > 0.0f);
    CHECK (fine < coarse / 3.0f);
}

TEST_CASE ("Knob: mouse wheel adjusts", "[ui][knob]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    EditorFixture f (p);
    auto* slider = dynamic_cast<juce::Slider*> (findKnob (*f.editor, id::mix)->getChildComponent (0));
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = -0.5f;
    slider->mouseWheelMove (mouseAt (*slider, slider->getLocalBounds().getCentre().toFloat()), wheel);
    CHECK (plain (p, id::mix) < 100.0f);
}

TEST_CASE ("Knob: click on a scale mark jumps to it; label toggles the value", "[ui][knob]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    EditorFixture f (p);
    auto* knob = findKnob (*f.editor, id::pitch);
    const float w = static_cast<float> (knob->getWidth()), h = static_cast<float> (knob->getHeight()) - 24.0f;
    const juce::Point<float> c (w * 0.5f, h * 0.5f);
    const float r = std::min (w, h) * 0.5f;
    // +6 st is at 3/4 of the -12..+12 range: angle = -135 deg + 0.75 * 270 deg = +67.5 deg.
    const float angle = juce::degreesToRadians (67.5f);
    knob->mouseDown (mouseAt (*knob, c.getPointOnCircumference (r * 0.80f, angle)));
    CHECK (plain (p, id::pitch) == 6.0f);

    CHECK_FALSE (knob->isShowingValue());
    knob->mouseDown (mouseAt (*knob, { w * 0.5f, static_cast<float> (knob->getHeight()) - 10.0f }));
    CHECK (knob->isShowingValue());
}

TEST_CASE ("Mode lever: clicking each label selects the mode", "[ui][lever]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    EditorFixture f (p);
    auto levers = findAll<metamorfo::ui::ToggleLever3> (*f.editor);
    REQUIRE (levers.size() == 1);
    auto* lever = levers.front();
    const float h = static_cast<float> (lever->getHeight());
    for (const int i : { 2, 1, 0, 2 })
    {
        const float y = h * (0.18f + 0.32f * static_cast<float> (i));
        lever->mouseDown (mouseAt (*lever, { 100.0f, y }));
        lever->mouseUp (mouseAt (*lever, { 100.0f, y }));
        CHECK (plain (p, id::mode) == static_cast<float> (i));
        CHECK (lever->position() == i);
    }
}

TEST_CASE ("LINK button toggles the parameter and FORMANT mirrors PITCH", "[ui][link]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    {
        EditorFixture f (p);
        auto* link = findButton (*f.editor, "LINK");
        REQUIRE (link != nullptr);
        link->setToggleState (true, juce::sendNotificationSync);
        CHECK (plain (p, id::link) == 1.0f);
    }
    EditorFixture f (p); // a fresh editor refreshes its state on construction
    CHECK (findKnob (*f.editor, id::formant)->isMirroring());
    CHECK_FALSE (findKnob (*f.editor, id::pitch)->isMirroring());
}

TEST_CASE ("Preset bar: next / previous / A-B buttons work", "[ui][presets]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    p.getPresets().load (0);
    EditorFixture f (p);
    auto* next = findButton (*f.editor, juce::String::fromUTF8 ("▶"));
    auto* prev = findButton (*f.editor, juce::String::fromUTF8 ("◀"));
    auto* ab = findButton (*f.editor, "A");
    REQUIRE ((next != nullptr && prev != nullptr && ab != nullptr));
    next->onClick();
    CHECK (p.getPresets().currentIndex() == 1);
    prev->onClick();
    CHECK (p.getPresets().currentIndex() == 0);
    ab->onClick();
    CHECK (p.getPresets().activeSlot() == 1);
}

TEST_CASE ("UI scale: 75-200 %, fixed aspect, remembered", "[ui][scale]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    const float saved = metamorfo::ui::MetamorfoEditor::loadSavedScale();
    MetamorfoProcessor p;
    {
        EditorFixture f (p);
        for (const float s : { 0.75f, 1.0f, 1.5f, 2.0f })
        {
            f.editor->setSize (juce::roundToInt (780 * s), juce::roundToInt (260 * s));
            CHECK (f.editor->getWidth() == juce::roundToInt (780 * s));
            CHECK (f.editor->getHeight() == juce::roundToInt (260 * s));
        }
        // User drag goes through the constrainer: limits and the fixed 3:1 aspect apply.
        auto* constrainer = f.editor->getConstrainer();
        REQUIRE (constrainer != nullptr);
        auto bounds = juce::Rectangle<int> (0, 0, 2000, 900);
        constrainer->checkBounds (bounds, f.editor->getBounds(), juce::Desktop::getInstance().getDisplays().getTotalBounds (true), false, false, true, true);
        CHECK (bounds.getWidth() <= 1560);
        CHECK (std::abs (static_cast<double> (bounds.getWidth()) / bounds.getHeight() - 3.0) < 0.02);

        // A host that imposes any size directly (B24): the canvas is scaled to fit inside
        // it, centred, never cropped.
        for (const auto size : { juce::Point<int> (2000, 900), juce::Point<int> (700, 400), juce::Point<int> (1200, 250) })
        {
            f.editor->setSize (size.x, size.y);
            juce::Component* content = nullptr;
            for (auto* child : f.editor->getChildren())
                if (child->getWidth() == 780 && child->getHeight() == 260)
                    content = child;
            REQUIRE (content != nullptr);
            const auto shown = content->getBounds().toFloat().transformedBy (content->getTransform());
            INFO ("editor " << size.x << "x" << size.y << " content " << shown.toString());
            CHECK (f.editor->getLocalBounds().toFloat().expanded (1.0f).contains (shown));
            CHECK ((std::abs (shown.getWidth() - static_cast<float> (size.x)) < 2.0f || std::abs (shown.getHeight() - static_cast<float> (size.y)) < 2.0f));
        }
    }
    metamorfo::ui::MetamorfoEditor::saveScale (1.25f);
    CHECK (metamorfo::ui::MetamorfoEditor::loadSavedScale() == 1.25f);
    metamorfo::ui::MetamorfoEditor::saveScale (saved);
}

TEST_CASE ("Text fits: preset names, captions and values stay inside their boxes", "[ui][layout]")
{
    if (! hasDisplay()) SKIP ("No X display available");
    MetamorfoProcessor p;
    EditorFixture f (p);
    juce::LookAndFeel& lnf = f.editor->getLookAndFeel();

    // Preset name button: every factory preset (+ the menu arrow) must fit.
    juce::TextButton* nameButton = nullptr;
    for (auto* b : findAll<juce::TextButton> (*f.editor))
        if (b->getButtonText().contains (juce::String::fromUTF8 ("▾")))
            nameButton = b;
    REQUIRE (nameButton != nullptr);
    const auto font = lnf.getTextButtonFont (*nameButton, nameButton->getHeight());
    for (const auto& e : p.getPresets().entries())
    {
        const float width = juce::GlyphArrangement::getStringWidth (font, e.name + juce::String::fromUTF8 ("  ▾"));
        INFO (e.name << " width " << width << " box " << nameButton->getWidth());
        CHECK (width < static_cast<float> (nameButton->getWidth()) - 8.0f);
    }

    // Knob captions and their value texts (worst case values).
    for (auto* knob : findAll<metamorfo::ui::Knob> (*f.editor))
    {
        auto* param = p.getParameters().getParameter (knob->parameterId());
        for (const float v : { 0.0f, 0.5f, 1.0f })
        {
            const auto text = param->getText (v, 64);
            const float width = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (18.0f)), text);
            INFO (knob->parameterId() << " value text '" << text << "' width " << width);
            CHECK (width < static_cast<float> (knob->getWidth()));
        }
    }
}
