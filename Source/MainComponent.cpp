#include "MainComponent.h"

static juce::String sk (const char* utf8) { return juce::String::fromUTF8 (utf8); }

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

//==============================================================================
namespace
{
    struct TestSound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct TestVoice : juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<TestSound*> (s) != nullptr; }
        void startNote (int note, float vel, juce::SynthesiserSound*, int) override
        {
            phase = 0.0;
            delta = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
            level = 0.25 * vel;
            env.setSampleRate (getSampleRate());
            env.setParameters ({ 0.003f, 0.25f, 0.5f, 0.25f });
            env.noteOn();
        }
        void stopNote (float, bool allowTail) override
        {
            if (allowTail) env.noteOff();
            else { env.reset(); clearCurrentNote(); }
        }
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            while (--num >= 0)
            {
                // trochu bohatsi ton nez cisty sinus
                const auto s = (float) ((std::sin (phase) + 0.3 * std::sin (2.0 * phase) + 0.12 * std::sin (3.0 * phase)) * level * env.getNextSample());
                for (int c = out.getNumChannels(); --c >= 0;)
                    out.addSample (c, start, s);
                ++start;
                phase += delta;
                if (! env.isActive()) { clearCurrentNote(); break; }
            }
        }
        double phase = 0, delta = 0, level = 0;
        juce::ADSR env;
    };
}

TestSynthProcessor::TestSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < 16; ++i) synth.addVoice (new TestVoice());
    synth.addSound (new TestSound());
}

void TestSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

//==============================================================================
PluginWindow::PluginWindow (juce::AudioProcessor& p, std::function<void()> onClose)
    : DocumentWindow (p.getName(), Col::steel, DocumentWindow::closeButton | DocumentWindow::minimiseButton), onCloseFn (std::move (onClose))
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

//==============================================================================
MainComponent::MainComponent (juce::ApplicationProperties& p) : props (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    formatManager.addDefaultFormats();

    // obnov ulozene nastavenie zvuku (ASIO zariadenie, buffer, MIDI vstupy)
    auto* settings = props.getUserSettings();
    const juce::File lastPlugin (settings->getValue ("lastPlugin"));
    std::unique_ptr<juce::XmlElement> lastDescXml (settings->getXmlValue ("lastPluginDesc"));
    if (std::unique_ptr<juce::XmlElement> kp { settings->getXmlValue ("knownPlugins") })
        knownPlugins.recreateFromXml (*kp);
    knownPlugins.addChangeListener (this);
    std::unique_ptr<juce::XmlElement> saved (settings->getXmlValue ("audioDevice"));
    deviceManager.initialise (0, 2, saved.get(), true);
    deviceManager.addChangeListener (this);

    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 0, 0, 2, 2, true, false, true, false);
    addAndMakeVisible (*deviceSelector);

    title.setText ("Korgus Sequencer", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    for (auto* l : { &pluginLabel, &infoLabel, &hintLabel })
    {
        l->setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (*l);
    }
    infoLabel.setColour (juce::Label::backgroundColourId, Col::lcdBg);
    infoLabel.setColour (juce::Label::textColourId, Col::lcdFg);
    infoLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    hintLabel.setFont (juce::FontOptions (13.0f));
    hintLabel.setText (sk ("Pluginy: klikni na „Pluginy a priečinky…“ → Možnosti… → Hľadať nové alebo zmenené VST3 pluginy. Tam nastavíš aj "
                           "priečinky, kde máš pluginy. Nájdené pluginy sa ukážu v zozname – dvojklikom ho načítaš. "
                           "Okno pluginu zavrieš krížikom, klávesom Esc alebo tlačidlom „Zavrieť okno pluginu“."), juce::dontSendNotification);

    for (auto* b : { &managerButton, &loadButton, &editorButton, &unloadButton })
        addAndMakeVisible (*b);
    managerButton.onClick = [this] { showPluginManager(); };
    loadButton.onClick   = [this] { chooseAndLoadPlugin(); };
    listLabel.setText (sk ("Nájdené pluginy (dvojklik = načítať):"), juce::dontSendNotification);
    addAndMakeVisible (listLabel);
    pluginList.setRowHeight (24);
    pluginList.setColour (juce::ListBox::outlineColourId, Col::border);
    pluginList.setOutlineThickness (1);
    addAndMakeVisible (pluginList);
    refreshPluginList();
    editorButton.onClick = [this]
    {
        if (pluginWindow != nullptr) { pluginWindow = nullptr; updateInfo(); }
        else showPluginWindow();
    };
    unloadButton.onClick = [this] { unloadPlugin(); };

    keyboardState.addListener (this);
    keyboard.setKeyWidth (22.0f);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyPressBaseOctave (4);
    keyboard.clearKeyMappings();
    {
        const char* keys = "awsedftgzhujkolp";   // slovenska klavesnica (QWERTZ)
        for (int i = 0; keys[i] != 0; ++i) keyboard.setKeyPressForNote (juce::KeyPress (keys[i]), i);
        keyboard.setKeyPressForNote (juce::KeyPress ('y'), 8);   // anglicka klavesnica
    }
    addAndMakeVisible (keyboard);

    deviceManager.addAudioCallback (&player);
    deviceManager.addMidiInputDeviceCallback ({}, &player);   // vsetky zapnute MIDI vstupy

    // posledny plugin
    juce::String err;
    juce::PluginDescription lastDesc;
    if (lastDescXml != nullptr && lastDesc.loadFromXml (*lastDescXml))
    {
        if (! loadPluginDescription (lastDesc, err))
            setProcessor (std::make_unique<TestSynthProcessor>());
    }
    else if (lastPlugin.existsAsFile() || lastPlugin.isDirectory())
    {
        if (! loadPluginFile (lastPlugin, err))
        {
                    setProcessor (std::make_unique<TestSynthProcessor>());
        }
    }
    else
        setProcessor (std::make_unique<TestSynthProcessor>());

    ready = true;
    setSize (1100, 720);
    startTimerHz (4);
    updateInfo();
}

MainComponent::~MainComponent()
{
    saveSettings();
    keyboardState.removeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &player);
    deviceManager.removeAudioCallback (&player);
    pluginWindow = nullptr;
    managerWindow = nullptr;
    knownPlugins.removeChangeListener (this);
    player.setProcessor (nullptr);
    processor = nullptr;
    deviceManager.removeChangeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::panel);
    g.setColour (Col::steel);
    g.fillRect (0, 0, getWidth(), 40);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    title.setBounds (r.removeFromTop (40).reduced (12, 6));
    keyboard.setBounds (r.removeFromBottom (110).reduced (8));
    r.reduce (8, 8);
    deviceSelector->setBounds (r.removeFromLeft (juce::jmin (560, r.getWidth() / 2)));
    r.removeFromLeft (12);
    auto row = r.removeFromTop (30);
    managerButton.setBounds (row.removeFromLeft (200)); row.removeFromLeft (6);
    loadButton.setBounds (row.removeFromLeft (120));
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    editorButton.setBounds (row.removeFromLeft (200)); row.removeFromLeft (6);
    unloadButton.setBounds (row.removeFromLeft (140));
    r.removeFromTop (6);
    pluginLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    infoLabel.setBounds (r.removeFromTop (112));
    r.removeFromTop (8);
    hintLabel.setBounds (r.removeFromBottom (54));
    listLabel.setBounds (r.removeFromTop (22));
    pluginList.setBounds (r.reduced (0, 2));
}

//==============================================================================
void MainComponent::handleNoteOn (juce::MidiKeyboardState*, int ch, int note, float vel)
{
    auto m = juce::MidiMessage::noteOn (ch, note, vel);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    player.getMidiMessageCollector().addMessageToQueue (m);
}

void MainComponent::handleNoteOff (juce::MidiKeyboardState*, int ch, int note, float vel)
{
    auto m = juce::MidiMessage::noteOff (ch, note, vel);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    player.getMidiMessageCollector().addMessageToQueue (m);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (src == &knownPlugins) refreshPluginList();
    saveSettings();
    updateInfo();
}

void MainComponent::timerCallback() { updateInfo(); }

void MainComponent::updateInfo()
{
    juce::String s;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double sr  = dev->getCurrentSampleRate();
        const int    buf = dev->getCurrentBufferSizeSamples();
        const int    out = dev->getOutputLatencyInSamples();
        auto ms = [sr] (int n) { return sr > 0 ? juce::String (1000.0 * n / sr, 1) + " ms" : juce::String ("?"); };
        s << sk ("Typ:        ") << dev->getTypeName() << "\n"
          << sk ("Zariadenie: ") << dev->getName() << "\n"
          << sk ("Vzorkovanie:") << " " << juce::String ((int) sr) << " Hz\n"
          << sk ("Buffer:     ") << buf << sk (" vzoriek = ") << ms (buf) << "\n"
          << sk ("Výstupná latencia podľa ovládača: ") << ms (out) << "\n"
          << sk ("Záťaž CPU:  ") << juce::String (deviceManager.getCpuUsage() * 100.0, 1) << " %";
    }
    else
    {
        s << sk ("Žiadne zvukové zariadenie nie je otvorené.\nVyber ho vľavo.");
    }
    infoLabel.setText (s, juce::dontSendNotification);

    const bool isPlugin = processor != nullptr && dynamic_cast<TestSynthProcessor*> (processor.get()) == nullptr;
    pluginLabel.setText (isPlugin ? sk ("Nástroj: ") + processor->getName()
                                  : sk ("Nástroj: testovací tón (žiadny VST)"), juce::dontSendNotification);
    editorButton.setEnabled (isPlugin && processor->hasEditor());
    editorButton.setButtonText (pluginWindow != nullptr ? sk ("Zavrieť okno pluginu") : sk ("Otvoriť okno pluginu"));
    unloadButton.setEnabled (isPlugin);
}

void MainComponent::saveSettings()
{
    if (! ready) return;
    auto* settings = props.getUserSettings();
    if (auto xml = deviceManager.createStateXml())
        settings->setValue ("audioDevice", xml.get());
    settings->setValue ("lastPlugin", pluginFile.getFullPathName());
    if (currentDesc != nullptr) { auto x = currentDesc->createXml(); settings->setValue ("lastPluginDesc", x.get()); }
    else settings->removeValue ("lastPluginDesc");
    if (auto kp = knownPlugins.createXml()) settings->setValue ("knownPlugins", kp.get());
    props.saveIfNeeded();
}

//==============================================================================
void MainComponent::chooseAndLoadPlugin()
{
    auto start = juce::File ("C:\\Program Files\\Common Files\\VST3");
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser = std::make_unique<juce::FileChooser> (sk ("Vyber VST3 plugin"), start, "*.vst3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              juce::String err;
                              if (! loadPluginFile (f, err))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          sk ("Plugin sa nedá načítať"), err);
                          });
}

bool MainComponent::loadPluginFile (const juce::File& f, juce::String& error)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* fmt : formatManager.getFormats())
        if (fmt->fileMightContainThisPluginType (f.getFullPathName()))
            fmt->findAllTypesForFile (types, f.getFullPathName());

    if (types.isEmpty())
    {
        error = sk ("V súbore sa nenašiel žiadny VST3 plugin:\n") + f.getFullPathName();
        return false;
    }

    // uprednostni nastroj (instrument) pred efektom
    auto* desc = types[0];
    for (auto* t : types)
        if (t->isInstrument) { desc = t; break; }

    for (auto* t : types) knownPlugins.addType (*t);   // pridaj aj do zoznamu
    return loadPluginDescription (*desc, error);
}

bool MainComponent::loadPluginDescription (const juce::PluginDescription& desc, juce::String& error)
{
    double sr = 44100.0; int bs = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        sr = dev->getCurrentSampleRate();
        bs = dev->getCurrentBufferSizeSamples();
    }

    auto instance = formatManager.createPluginInstance (desc, sr, bs, error);
    if (instance == nullptr)
    {
        if (error.isEmpty()) error = sk ("Plugin sa nepodarilo spustiť.");
        return false;
    }

    // stereo vystup, ak to plugin dovoli
    auto layout = instance->getBusesLayout();
    if (layout.outputBuses.size() > 0)
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (instance->checkBusesLayoutSupported (layout))
            instance->setBusesLayout (layout);
    }

    pluginFile = juce::File (desc.fileOrIdentifier);
    currentDesc = std::make_unique<juce::PluginDescription> (desc);
    setProcessor (std::move (instance));
    saveSettings();
    if (ready) showPluginWindow();
    return true;
}

//==============================================================================
void MainComponent::refreshPluginList()
{
    pluginRows.clear();
    for (const auto& t : knownPlugins.getTypes()) pluginRows.add (t);
    std::sort (pluginRows.begin(), pluginRows.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        if (a.isInstrument != b.isInstrument) return a.isInstrument;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    pluginList.updateContent();
    pluginList.repaint();
}

int MainComponent::getNumRows() { return pluginRows.size(); }

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, pluginRows.size())) return;
    const auto& d = pluginRows.getReference (row);
    const bool current = currentDesc != nullptr && d.isDuplicateOf (*currentDesc);
    g.fillAll (selected ? Col::steel : (row % 2 ? juce::Colour (0xfff2f3f5) : juce::Colours::white));
    g.setColour (selected ? juce::Colours::white : Col::text);
    g.setFont (juce::FontOptions (14.0f, current ? juce::Font::bold : juce::Font::plain));
    g.drawText (d.name, 8, 0, w - 220, h, juce::Justification::centredLeft, true);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : Col::border.darker (0.4f));
    g.drawText ((d.isInstrument ? sk ("nástroj") : sk ("efekt")) + "  ·  " + d.manufacturerName,
                w - 210, 0, 202, h, juce::Justification::centredRight, true);
}

void MainComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { returnKeyPressed (row); }

void MainComponent::returnKeyPressed (int row)
{
    if (! juce::isPositiveAndBelow (row, pluginRows.size())) return;
    juce::String err;
    if (! loadPluginDescription (pluginRows.getReference (row), err))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, sk ("Plugin sa nedá načítať"), err);
    pluginList.repaint();
}

void MainComponent::showPluginManager()
{
    if (managerWindow != nullptr) { managerWindow->toFront (true); return; }
    auto pedal = props.getUserSettings()->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
    juce::Component::SafePointer<MainComponent> sp (this);
    managerWindow = std::make_unique<PluginManagerWindow> (formatManager, knownPlugins, pedal, props.getUserSettings(), [sp]
    {
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->managerWindow = nullptr; sp->saveSettings(); } });
    });
}

void MainComponent::unloadPlugin()
{
    pluginFile = juce::File();
    currentDesc = nullptr;
    setProcessor (std::make_unique<TestSynthProcessor>());
    saveSettings();
}

void MainComponent::setProcessor (std::unique_ptr<juce::AudioProcessor> p)
{
    pluginWindow = nullptr;
    player.setProcessor (nullptr);
    processor = std::move (p);
    player.setProcessor (processor.get());
    updateInfo();
}

void MainComponent::showPluginWindow()
{
    if (processor == nullptr || ! processor->hasEditor()) return;
    if (pluginWindow != nullptr) { pluginWindow->toFront (true); return; }
    juce::Component::SafePointer<MainComponent> sp (this);
    pluginWindow = std::make_unique<PluginWindow> (*processor, [sp]
    {
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->pluginWindow = nullptr; sp->updateInfo(); } });
    });
    updateInfo();
}
