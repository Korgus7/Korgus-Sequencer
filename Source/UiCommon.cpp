#include "UiCommon.h"

juce::String sk (const char* utf8) { return juce::String::fromUTF8 (utf8); }

namespace Col
{
    const juce::Colour steel   (0xff2d4362);
    const juce::Colour panel   (0xffc0c0c0);
    const juce::Colour panelLo (0xffb2b2b2);
    const juce::Colour button  (0xffd7dade);
    const juce::Colour border  (0xff7c8796);
    const juce::Colour text    (0xff1b2533);
    const juce::Colour lcdBg   (0xff18212c);
    const juce::Colour lcdFg   (0xffc3d39a);
}

//==============================================================================
KorgusLookAndFeel::KorgusLookAndFeel()
{
    setColourScheme ({ Col::panel, Col::panel, Col::panelLo, Col::border, Col::text,
                       Col::button, juce::Colours::white, Col::steel, Col::text });
    setColour (juce::TextButton::buttonColourId, Col::button);
    setColour (juce::TextButton::buttonOnColourId, Col::steel);
    setColour (juce::TextButton::textColourOffId, Col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, Col::border);
    setColour (juce::ComboBox::textColourId, Col::text);
    setColour (juce::ComboBox::arrowColourId, Col::text);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::white);
    setColour (juce::PopupMenu::textColourId, Col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Col::steel);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, Col::text);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::white);
    setColour (juce::ListBox::outlineColourId, Col::border);
    setColour (juce::ToggleButton::textColourId, Col::text);
    setColour (juce::ToggleButton::tickColourId, Col::steel);
    setColour (juce::ToggleButton::tickDisabledColourId, Col::border);
    setColour (juce::Slider::thumbColourId, Col::steel);
    setColour (juce::Slider::trackColourId, Col::border);
    setColour (juce::Slider::backgroundColourId, Col::panelLo);
    setColour (juce::ScrollBar::thumbColourId, Col::border);
}

void KorgusLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bg;
    if (down)      c = c.darker (0.12f);
    else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (b.getToggleState() ? Col::steel.darker (0.3f) : Col::border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void KorgusLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> r (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path p;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (p);
}

PluginWindow::PluginWindow (juce::AudioProcessor& p, const juce::String& title, std::function<void()> onClose)
    : DocumentWindow (title, Col::steel, DocumentWindow::closeButton | DocumentWindow::minimiseButton), onCloseFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    auto* ed = p.createEditorIfNeeded();
    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced (20);
    area.removeFromTop (40);   // miesto pre titulok okna, aby sa dalo vzdy zavriet

    if (ed == nullptr)
    {
        centreWithSize (300, 100);
    }
    else if (ed->getWidth() <= area.getWidth() && ed->getHeight() <= area.getHeight())
    {
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        setTopLeftPosition (area.getX() + (area.getWidth() - getWidth()) / 2,
                            area.getY() + juce::jmax (0, (area.getHeight() - getHeight()) / 3));
    }
    else
    {
        // plugin je vacsi nez obrazovka - vlozime ho do posuvneho okna
        auto* vp = new juce::Viewport();
        vp->setViewedComponent (ed, false);
        vp->setScrollBarsShown (true, true);
        editorHolder.reset (ed);
        const int w = juce::jmin (ed->getWidth() + vp->getScrollBarThickness(), area.getWidth());
        const int h = juce::jmin (ed->getHeight() + vp->getScrollBarThickness(), area.getHeight());
        vp->setSize (w, h);
        setContentOwned (vp, true);
        setResizable (true, false);
        setTopLeftPosition (area.getX() + (area.getWidth() - w) / 2, area.getY());
    }
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
    editorHolder = nullptr;
}

PluginManagerWindow::PluginManagerWindow (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& kl, const juce::File& pedal,
                                          juce::PropertiesFile* props, std::function<void()> onClose)
    : DocumentWindow (juce::String::fromUTF8 ("Pluginy – zoznam a priečinky"), Col::steel, DocumentWindow::closeButton),
      onCloseFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    auto* list = new juce::PluginListComponent (fm, kl, pedal, props, true);
    list->setSize (820, 520);
    setContentOwned (list, true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

