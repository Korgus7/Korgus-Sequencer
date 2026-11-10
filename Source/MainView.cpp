#include "MainView.h"
#include "BinaryData.h"
#include "SongParse.h"

//==============================================================================
SettingsComponent::SettingsComponent (juce::AudioDeviceManager& d, std::function<void()> onPlugins)
    : dm (d), selector (d, 0, 2, 2, 2, true, false, true, false), onPluginsFn (std::move (onPlugins))
{
    addAndMakeVisible (selector);
    info.setColour (juce::Label::backgroundColourId, Col::lcdBg);
    info.setColour (juce::Label::textColourId, Col::lcdFg);
    info.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);
    hint.setText (sk ("Najlepšia odozva je s typom zariadenia ASIO a malým bufferom (64 – 256). "
                      "Ak zvuk praská, zväčši buffer. MIDI klávesy zaškrtni v zozname MIDI vstupov."), juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);
    pluginsButton.setButtonText (sk ("Pluginy a priečinky…"));
    pluginsButton.onClick = [this] { if (onPluginsFn) onPluginsFn(); };
    addAndMakeVisible (pluginsButton);
    setSize (980, 520);
    startTimerHz (3);
    timerCallback();
}

void SettingsComponent::paint (juce::Graphics& g) { g.fillAll (Col::panel); }

void SettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (10);
    selector.setBounds (r.removeFromLeft (540));
    r.removeFromLeft (10);
    pluginsButton.setBounds (r.removeFromTop (30).removeFromLeft (220));
    r.removeFromTop (10);
    info.setBounds (r.removeFromTop (130));
    r.removeFromTop (10);
    hint.setBounds (r.removeFromTop (80));
}

void SettingsComponent::timerCallback()
{
    juce::String s;
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        const int buf = dev->getCurrentBufferSizeSamples(), out = dev->getOutputLatencyInSamples();
        auto ms = [sr] (int n) { return sr > 0 ? juce::String (1000.0 * n / sr, 1) + " ms" : juce::String ("?"); };
        s << sk ("Typ:        ") << dev->getTypeName() << "\n"
          << sk ("Zariadenie: ") << dev->getName() << "\n"
          << sk ("Vzorkovanie: ") << juce::String ((int) sr) << " Hz\n"
          << sk ("Buffer:     ") << buf << sk (" vzoriek = ") << ms (buf) << "\n"
          << sk ("Výstupná latencia: ") << ms (out) << "\n"
          << sk ("Záťaž CPU:  ") << juce::String (dm.getCpuUsage() * 100.0, 1) << " %";
    }
    else s << sk ("Žiadne zvukové zariadenie nie je otvorené.");
    info.setText (s, juce::dontSendNotification);
}

//==============================================================================
namespace
{
    struct SimpleWindow : juce::DocumentWindow
    {
        SimpleWindow (const juce::String& title, juce::Component* content, std::function<void()> onClose)
            : DocumentWindow (title, Col::steel, DocumentWindow::closeButton), closeFn (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { if (closeFn) closeFn(); }
        std::function<void()> closeFn;
    };

    juce::var obj() { return juce::var (new juce::DynamicObject()); }
    void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { if (auto* d = o.getDynamicObject()) d->setProperty (k, v); }
}

MainView::MainView (juce::ApplicationProperties& p) : props (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    formatManager.addDefaultFormats();

    dataDir = props.getUserSettings()->getFile().getParentDirectory();
    dataDir.createDirectory();

    auto* settings = props.getUserSettings();
    if (std::unique_ptr<juce::XmlElement> kp { settings->getXmlValue ("knownPlugins") })
        knownPlugins.recreateFromXml (*kp);
    knownPlugins.addChangeListener (this);

    std::unique_ptr<juce::XmlElement> saved (settings->getXmlValue ("audioDevice"));
    deviceManager.initialise (2, 2, saved.get(), true);
    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (&engine);
    deviceManager.addMidiInputDeviceCallback ({}, &engine);

    engine.onMidiIn = [this] (int pitch, float vel, bool on, double pos)
    {
        auto o = obj();
        put (o, "pitch", pitch); put (o, "vel", vel); put (o, "on", on); put (o, "pos", pos);
        emit ("midiIn", o);
    };

    loadSession();

    web = std::make_unique<juce::WebBrowserComponent> (makeOptions());
    addAndMakeVisible (*web);
   #if JUCE_LINUX
    // na Linuxe velke stranky cez resource provider padaju - nacitame subor z disku
    auto page = dataDir.getChildFile ("ui").getChildFile ("index.html");
    page.getParentDirectory().createDirectory();
    page.replaceWithData (BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
    web->goToURL (juce::URL (page).toString (false));
   #else
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif

    setSize (1320, 860);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    saveSession();
    saveSettings();
    pluginWindows.clear();
    settingsWindow = nullptr;
    managerWindow = nullptr;
    knownPlugins.removeChangeListener (this);
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &engine);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    web = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainView::resized()
{
    if (web != nullptr) web->setBounds (getLocalBounds());
}

juce::WebBrowserComponent::Options MainView::makeOptions()
{
    using O = juce::WebBrowserComponent::Options;
    auto o = O{}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withEventListener ("song",        [this] (const juce::var& v) { onSong (v); })
        .withEventListener ("transport",   [this] (const juce::var& v) { onTransport (v); })
        .withEventListener ("note",        [this] (const juce::var& v) { onNote (v); })
        .withEventListener ("selTrack",    [this] (const juce::var& v) { engine.setSelectedSlot (engine.getOrCreateSlot (v["track"].toString())); })
        .withEventListener ("trackPlugin", [this] (const juce::var& v) { onTrackPlugin (v); })
        .withEventListener ("openPlugin",  [this] (const juce::var& v) { onOpenPlugin (v); })
        .withEventListener ("showSettings",[this] (const juce::var&)   { showSettings(); })
        .withEventListener ("showPlugins", [this] (const juce::var&)   { showPluginManager(); })
        .withEventListener ("saveFile",    [this] (const juce::var& v) { onSaveFile (v); })
        .withEventListener ("openSong",    [this] (const juce::var& v) { onOpenSong (v); })
        .withEventListener ("ready",       [this] (const juce::var&)   { onReady(); });
   #if JUCE_WINDOWS
    o = o.withBackend (O::Backend::webview2)
         .withWinWebView2Options (O::WinWebView2{}
                                     .withUserDataFolder (dataDir.getChildFile ("WebView2"))
                                     .withBackgroundColour (juce::Colour (0xff3b4b5f))
                                     .withStatusBarDisabled());
   #endif
    return o;
}

std::optional<juce::WebBrowserComponent::Resource> MainView::getResource (const juce::String& url)
{
    const auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty() || path == "index.html")
    {
        std::vector<std::byte> data ((size_t) BinaryData::index_htmlSize);
        std::memcpy (data.data(), BinaryData::index_html, data.size());
        return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
    }
    return std::nullopt;
}

void MainView::emit (const juce::String& id, const juce::var& v)
{
    if (web != nullptr) web->emitEventIfBrowserIsVisible (id, v);
}

//==============================================================================
void MainView::onSong (const juce::var& v)
{
    auto sd = parseSong (v, [this] (const juce::String& id) { return engine.getOrCreateSlot (id); });
    if (sd == nullptr) return;
    engine.setSong (std::move (sd));

    if (! sessionApplied)
    {
        sessionApplied = true;
        if (pendingSession.isObject()) restorePluginStates (pendingSession);
        pendingSession = juce::var();
    }
}

void MainView::onTransport (const juce::var& v)
{
    const auto cmd = v["cmd"].toString();
    if (cmd == "play")        engine.play ((double) v["pos"], (int) v.getProperty ("countIn", 0));
    else if (cmd == "stop")   engine.stop();
    else if (cmd == "setPos") engine.setPosition ((double) v["pos"]);
}

void MainView::onNote (const juce::var& v)
{
    const int slot = engine.getOrCreateSlot (v["track"].toString());
    engine.liveNote (slot, (int) v["pitch"], (float) (double) v.getProperty ("vel", 0.8), (bool) v["on"]);
}

void MainView::onTrackPlugin (const juce::var& v)
{
    const auto trackId = v["track"].toString(), uid = v["uid"].toString();
    if (uid.isEmpty())
    {
        pluginWindows.erase (trackId);
        engine.setTrackPlugin (engine.getOrCreateSlot (trackId), nullptr);
        trackDesc.erase (trackId);
        sendTrackPlugins();
        saveSession();
        return;
    }
    for (const auto& d : knownPlugins.getTypes())
        if (d.createIdentifierString() == uid)
        {
            if (loadPluginForTrack (trackId, d, nullptr))
                onOpenPlugin (v);
            return;
        }
}

bool MainView::loadPluginForTrack (const juce::String& trackId, const juce::PluginDescription& d, const juce::MemoryBlock* state)
{
    double sr = 44100; int bs = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice()) { sr = dev->getCurrentSampleRate(); bs = dev->getCurrentBufferSizeSamples(); }
    juce::String err;
    auto inst = formatManager.createPluginInstance (d, sr, bs, err);
    if (inst == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, sk ("Plugin sa nedá načítať"),
                                                d.name + "\n" + err);
        return false;
    }
    auto layout = inst->getBusesLayout();
    if (layout.outputBuses.size() > 0)
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (inst->checkBusesLayoutSupported (layout)) inst->setBusesLayout (layout);
    }
    if (state != nullptr && state->getSize() > 0)
        inst->setStateInformation (state->getData(), (int) state->getSize());

    pluginWindows.erase (trackId);
    engine.setTrackPlugin (engine.getOrCreateSlot (trackId), std::move (inst));
    trackDesc[trackId] = d;
    sendTrackPlugins();
    saveSession();
    return true;
}

void MainView::onOpenPlugin (const juce::var& v)
{
    const auto trackId = v["track"].toString();
    auto it = pluginWindows.find (trackId);
    if (it != pluginWindows.end()) { it->second->toFront (true); return; }
    auto* p = engine.getPlugin (engine.getOrCreateSlot (trackId));
    if (p == nullptr || ! p->hasEditor()) return;
    juce::Component::SafePointer<MainView> sp (this);
    const juce::String title = p->getName() + "  –  " + v.getProperty ("name", "").toString();
    pluginWindows[trackId] = std::make_unique<PluginWindow> (*p, title, [sp, trackId]
    {
        juce::MessageManager::callAsync ([sp, trackId] { if (sp != nullptr) sp->pluginWindows.erase (trackId); });
    });
}

void MainView::onReady()
{
    sendPluginList();
    sendTrackPlugins();
}

void MainView::sendPluginList()
{
    juce::Array<juce::PluginDescription> list;
    for (const auto& d : knownPlugins.getTypes()) list.add (d);
    std::sort (list.begin(), list.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        if (a.isInstrument != b.isInstrument) return a.isInstrument;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    juce::Array<juce::var> arr;
    for (const auto& d : list)
    {
        auto o = obj();
        put (o, "uid", d.createIdentifierString()); put (o, "name", d.name);
        put (o, "maker", d.manufacturerName); put (o, "inst", d.isInstrument);
        arr.add (o);
    }
    emit ("plugins", juce::var (arr));
}

void MainView::sendTrackPlugins()
{
    auto o = obj();
    for (const auto& [id, d] : trackDesc)
    {
        auto t = obj();
        put (t, "name", d.name); put (t, "uid", d.createIdentifierString());
        put (o, id, t);
    }
    emit ("trackPlugins", o);
}

//==============================================================================
juce::var MainView::collectPluginStates()
{
    auto o = obj();
    for (const auto& [id, d] : trackDesc)
    {
        auto* p = engine.getPlugin (engine.getOrCreateSlot (id));
        if (p == nullptr) continue;
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        auto t = obj();
        if (auto x = d.createXml()) put (t, "desc", x->toString (juce::XmlElement::TextFormat().singleLine()));
        put (t, "state", mb.toBase64Encoding());
        put (o, id, t);
    }
    return o;
}

void MainView::restorePluginStates (const juce::var& vst)
{
    if (auto* d = vst.getDynamicObject())
    {
        for (const auto& prop : d->getProperties())
        {
            const auto trackId = prop.name.toString();
            const auto xml = juce::parseXML (prop.value["desc"].toString());
            juce::PluginDescription desc;
            if (xml == nullptr || ! desc.loadFromXml (*xml)) continue;
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (prop.value["state"].toString());
            loadPluginForTrack (trackId, desc, &mb);
        }
    }
}

void MainView::saveSession()
{
    dataDir.getChildFile ("session.json").replaceWithText (juce::JSON::toString (collectPluginStates()));
}

void MainView::loadSession()
{
    const auto f = dataDir.getChildFile ("session.json");
    if (f.existsAsFile()) pendingSession = juce::JSON::parse (f.loadFileAsString());
}

void MainView::saveSettings()
{
    auto* settings = props.getUserSettings();
    if (auto xml = deviceManager.createStateXml()) settings->setValue ("audioDevice", xml.get());
    if (auto kp = knownPlugins.createXml()) settings->setValue ("knownPlugins", kp.get());
    props.saveIfNeeded();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (src == &knownPlugins) sendPluginList();
    saveSettings();
}

//==============================================================================
void MainView::onSaveFile (const juce::var& v)
{
    const auto name = v["name"].toString(), kind = v["kind"].toString();
    juce::MemoryBlock data;
    if (kind == "s92")
    {
        auto j = juce::JSON::parse (v["text"].toString());
        if (auto* d = j.getDynamicObject()) d->setProperty ("vst", collectPluginStates());
        const auto txt = juce::JSON::toString (j, true);
        data.append (txt.toRawUTF8(), txt.getNumBytesAsUTF8());
    }
    else if (v.hasProperty ("b64")) data.fromBase64Encoding (v["b64"].toString());
    else { const auto t = v["text"].toString(); data.append (t.toRawUTF8(), t.getNumBytesAsUTF8()); }

    const auto ext = name.fromLastOccurrenceOf (".", false, false);
    chooser = std::make_unique<juce::FileChooser> (sk ("Uložiť"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name),
                                                   "*." + ext);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [data] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f != juce::File()) f.replaceWithData (data.getData(), data.getSize());
                          });
}

void MainView::onOpenSong (const juce::var&)
{
    chooser = std::make_unique<juce::FileChooser> (sk ("Otvoriť pieseň"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.s92;*.json");
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (sp == nullptr || ! f.existsAsFile()) return;
                              auto j = juce::JSON::parse (f.loadFileAsString());
                              if (! j.isObject()) return;
                              const auto vst = j["vst"];
                              if (auto* d = j.getDynamicObject()) d->removeProperty ("vst");
                              sp->sessionApplied = true;
                              auto o = obj();
                              put (o, "text", juce::JSON::toString (j, true));
                              sp->emit ("songLoaded", o);
                              juce::MessageManager::callAsync ([sp, vst] { if (sp != nullptr && vst.isObject()) sp->restorePluginStates (vst); });
                          });
}

void MainView::showSettings()
{
    if (settingsWindow != nullptr) { settingsWindow->toFront (true); return; }
    juce::Component::SafePointer<MainView> sp (this);
    settingsWindow = std::make_unique<SimpleWindow> (sk ("Nastavenia zvuku"),
        new SettingsComponent (deviceManager, [sp] { if (sp != nullptr) sp->showPluginManager(); }),
        [sp] { juce::MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->settingsWindow = nullptr; sp->saveSettings(); } }); });
}

void MainView::showPluginManager()
{
    if (managerWindow != nullptr) { managerWindow->toFront (true); return; }
    auto pedal = dataDir.getChildFile ("RecentlyCrashedPluginsList");
    juce::Component::SafePointer<MainView> sp (this);
    managerWindow = std::make_unique<PluginManagerWindow> (formatManager, knownPlugins, pedal, props.getUserSettings(), [sp]
    {
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->managerWindow = nullptr; sp->saveSettings(); } });
    });
}

void MainView::timerCallback()
{
    const double p = engine.getPosition();
    const bool pl = engine.isPlaying(), cn = engine.isCounting();
    if (std::abs (p - lastSentPos) > 0.001 || pl != lastPlaying || cn != lastCounting || cn)
    {
        auto o = obj();
        put (o, "pos", p); put (o, "playing", pl); put (o, "counting", cn);
        put (o, "beat", engine.getCountBeat()); put (o, "total", engine.getCountTotal());
        emit ("pos", o);
        lastSentPos = p; lastPlaying = pl; lastCounting = cn;
    }
}
