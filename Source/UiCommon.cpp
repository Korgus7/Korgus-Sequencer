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

namespace
{
    // lista s velkostou nad pluginom + samotny plugin (pripadne v posuvnom okne)
    class PluginHolder : public juce::Component, private juce::ComponentListener
    {
    public:
        static constexpr int barH = 26;

        PluginHolder (juce::AudioProcessorEditor* e, float scale, std::function<void (float)> onScale)
            : ed (e), onScaleFn (std::move (onScale))
        {
            label.setText (juce::String::fromUTF8 ("Veľkosť pluginu"), juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, juce::Colour (0xffe4e9ef));
            label.setFont (juce::FontOptions (13.0f));
            addAndMakeVisible (label);
            const int pct[] = { 75, 100, 125, 150, 175, 200, 250 };
            for (int k = 0; k < 7; ++k) size.addItem (juce::String (pct[k]) + " %", pct[k]);
            size.setSelectedId (juce::roundToInt (scale * 100.0f) / 25 * 25 > 0 ? juce::roundToInt (scale * 100.0f) : 100, juce::dontSendNotification);
            if (size.getSelectedId() == 0) size.setSelectedId (100, juce::dontSendNotification);
            size.onChange = [this] { applyScale ((float) size.getSelectedId() / 100.0f, true); };
            addAndMakeVisible (size);

            if (ed != nullptr)
            {
                if (std::abs (scale - 1.0f) > 0.01f) ed->setScaleFactor (scale);
                fitToScreen();
                box.addAndMakeVisible (ed);
                addAndMakeVisible (box);
                ed->addComponentListener (this);
                updateSize();
            }
            else setSize (300, barH + 60);
        }
        ~PluginHolder() override
        {
            if (ed != nullptr) ed->removeComponentListener (this);
            vp = nullptr;
            delete ed;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff2a3442));
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto bar = r.removeFromTop (barH).reduced (6, 3);
            label.setBounds (bar.removeFromLeft (110));
            size.setBounds (bar.removeFromLeft (90));
            if (vp != nullptr) vp->setBounds (r);
            else box.setTopLeftPosition (r.getPosition());
        }

    private:
        static juce::Rectangle<int> screenArea()
        {
            auto a = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced (20);
            a.removeFromTop (40 + barH);
            return a;
        }
        // velkost editora po zvacseni (setScaleFactor meni transformaciu, nie getWidth)
        juce::Rectangle<int> edSize() const { return ed->getLocalBounds().transformedBy (ed->getTransform()).withZeroOrigin(); }
        bool tooBig() const
        {
            const auto a = screenArea();
            const auto e = edSize();
            return e.getWidth() > a.getWidth() || e.getHeight() > a.getHeight();
        }
        // plugin s menitelnou velkostou zmensime na obrazovku
        void fitToScreen()
        {
            if (ed == nullptr || ! ed->isResizable() || ! tooBig()) return;
            const auto a = screenArea();
            ed->setSize (juce::jmin (ed->getWidth(), a.getWidth()), juce::jmin (ed->getHeight(), a.getHeight()));
        }
        void applyScale (float s, bool save)
        {
            if (ed == nullptr) return;
            ed->setScaleFactor (s);
            fitToScreen();
            updateSize();
            if (auto* w = findParentComponentOfClass<juce::ResizableWindow>()) w->setContentComponentSize (getWidth(), getHeight());
            if (save && onScaleFn) onScaleFn (s);
        }
        void updateSize()
        {
            if (ed == nullptr) return;
            // editor so zmenenou mierkou je v obale velkom ako jeho skutocny obraz
            const auto es = edSize();
            ed->setTopLeftPosition (0, 0);
            box.setSize (es.getWidth(), es.getHeight());
            const bool big = tooBig();
            if (big && vp == nullptr)
            {
                removeChildComponent (&box);
                vp = std::make_unique<juce::Viewport>();
                vp->setViewedComponent (&box, false);
                vp->setScrollBarsShown (true, true);
                addAndMakeVisible (*vp);
            }
            else if (! big && vp != nullptr)
            {
                vp->setViewedComponent (nullptr, false);
                vp = nullptr;
                addAndMakeVisible (box);
            }
            const auto a = screenArea();
            const auto e = edSize();
            const int w = juce::jmax (300, vp != nullptr ? juce::jmin (e.getWidth() + 16, a.getWidth()) : e.getWidth());
            const int h = (vp != nullptr ? juce::jmin (e.getHeight() + 16, a.getHeight()) : e.getHeight()) + barH;
            setSize (w, h);
            resized();
        }
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (wasResized) updateSize();
        }

        juce::AudioProcessorEditor* ed = nullptr;
        juce::Component box;
        std::unique_ptr<juce::Viewport> vp;
        juce::Label label;
        juce::ComboBox size;
        std::function<void (float)> onScaleFn;
    };
}

PluginWindow::PluginWindow (juce::AudioProcessor& p, const juce::String& title, std::function<void()> onClose,
                            float scale, std::function<void (float)> onScale)
    : DocumentWindow (title, Col::steel, DocumentWindow::closeButton | DocumentWindow::minimiseButton), onCloseFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    auto* holder = new PluginHolder (p.createEditorIfNeeded(), scale, std::move (onScale));
    setContentOwned (holder, true);
    setResizable (false, false);
    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced (20);
    area.removeFromTop (40);   // miesto pre titulok okna, aby sa dalo vzdy zavriet
    setTopLeftPosition (area.getX() + juce::jmax (0, (area.getWidth() - getWidth()) / 2),
                        area.getY() + juce::jmax (0, (area.getHeight() - getHeight()) / 3));
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
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

