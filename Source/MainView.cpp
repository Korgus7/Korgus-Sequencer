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
    // VST3 pluginy vedla programu (napr. Korgus.vst3) sa pridaju samy; po aktualizacii suboru sa nacitaju znova
    {
        // stara rozbalena kopia z nastaveni uz netreba
        const auto oldDir = dataDir.getChildFile ("Plugins");
        for (const auto& d : knownPlugins.getTypes())
            if (juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier).isAChildOf (oldDir))
                knownPlugins.removeType (d);
        oldDir.deleteRecursively();

        const auto files = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory()
                               .findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3");
        for (auto* fmt : formatManager.getFormats())
        {
            if (fmt->getName() != "VST3") continue;
            for (const auto& f : files)
            {
                juce::OwnedArray<juce::PluginDescription> found;
                knownPlugins.scanAndAddFile (f.getFullPathName(), true, found, *fmt);
            }
        }
    }
    knownPlugins.addChangeListener (this);

    std::unique_ptr<juce::XmlElement> saved (settings->getXmlValue ("audioDevice"));
    deviceManager.initialise (2, 2, saved.get(), true);
    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (&engine);
    deviceManager.addMidiInputDeviceCallback ({}, &engine);

    store = std::make_unique<AudioStore> (dataDir.getChildFile ("audio"));
    if (auto* dev = deviceManager.getCurrentAudioDevice()) store->setSampleRate (dev->getCurrentSampleRate());

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
        .withEventListener ("selTrack",    [this] (const juce::var& v) { onSelTrack (v); })
        .withEventListener ("trackFx",     [this] (const juce::var& v) { onTrackFx (v); })
        .withEventListener ("openFx",      [this] (const juce::var& v) { onOpenFx (v); })
        .withEventListener ("trackPlugin", [this] (const juce::var& v) { onTrackPlugin (v); })
        .withEventListener ("openPlugin",  [this] (const juce::var& v) { onOpenPlugin (v); })
        .withEventListener ("showSettings",[this] (const juce::var&)   { showSettings(); })
        .withEventListener ("showPlugins", [this] (const juce::var&)   { showPluginManager(); })
        .withEventListener ("saveFile",    [this] (const juce::var& v) { onSaveFile (v); })
        .withEventListener ("openSong",    [this] (const juce::var& v) { onOpenSong (v); })
        .withEventListener ("importAudio", [this] (const juce::var& v) { onImportAudio (v); })
        .withEventListener ("audioChunk",  [this] (const juce::var& v) { onAudioChunk (v); })
        .withEventListener ("needPeaks",   [this] (const juce::var& v) { onNeedPeaks (v); })
        .withEventListener ("recAudio",    [this] (const juce::var& v) { onRecAudio (v); })
        .withEventListener ("exportWav",   [this] (const juce::var& v) { onExportWav (v); })
        .withEventListener ("aiSetKey",    [this] (const juce::var& v)
                            {
                                props.getUserSettings()->setValue ("openrouterKey", v["key"].toString().trim());
                                props.saveIfNeeded();
                                sendAiState();
                            })
        .withEventListener ("aiRequest",   [this] (const juce::var& v) { onAiRequest (v); })
        .withEventListener ("drumHit",     [this] (const juce::var& v)
                            {
                                engine.triggerSample (engine.getOrCreateSlot (v["track"].toString()), store->get (v["id"].toString()),
                                                      (float) (double) v.getProperty ("vel", 1.0), (int) v.getProperty ("lane", 0));
                            })
        .withEventListener ("importSample",[this] (const juce::var& v)
                            {
                                const auto track = v["track"].toString();
                                const int lane = (int) v["lane"];
                                chooser = std::make_unique<juce::FileChooser> (sk ("Zvuk pre bicie (WAV, MP3)"),
                                                                               juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                                               "*.wav;*.mp3;*.ogg;*.flac;*.aif;*.aiff");
                                juce::Component::SafePointer<MainView> sp (this);
                                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                      [sp, track, lane] (const juce::FileChooser& fc)
                                                      {
                                                          const auto f = fc.getResult();
                                                          if (sp == nullptr || ! f.existsAsFile()) return;
                                                          const auto id = sp->store->importFile (f);
                                                          auto o = obj();
                                                          put (o, "track", track); put (o, "lane", lane);
                                                          put (o, "name", f.getFileNameWithoutExtension());
                                                          if (id.isEmpty()) put (o, "error", true); else put (o, "id", id);
                                                          sp->emit ("sampleImported", o);
                                                      });
                            })
        .withEventListener ("autosave",    [this] (const juce::var& v)
                            {
                                const auto t = v["text"].toString();
                                if (t.length() > 10) dataDir.getChildFile ("lastsong.json").replaceWithText (t);
                            })
        .withEventListener ("copyPlugin",  [this] (const juce::var& v)
                            {
                                const auto from = v["from"].toString(), to = v["to"].toString();
                                auto it = trackDesc.find (from);
                                auto* p = engine.getPlugin (engine.getOrCreateSlot (from));
                                if (it == trackDesc.end() || p == nullptr) return;
                                juce::MemoryBlock mb;
                                p->getStateInformation (mb);
                                const auto d = it->second;
                                loadPluginForTrack (to, d, &mb);
                            })
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
std::unique_ptr<SongData> MainView::parseCurrent (const juce::var& v)
{
    return parseSong (v, [this] (const juce::String& id) { return engine.getOrCreateSlot (id); },
                         [this] (const juce::String& id) { return store->get (id); });
}

void MainView::onSong (const juce::var& v)
{
    lastSong = v;
    auto sd = parseCurrent (v);
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

void MainView::onSelTrack (const juce::var& v)
{
    engine.setSelectedSlot (engine.getOrCreateSlot (v["track"].toString()));
    engine.setMidiFilter (v.getProperty ("midiIn", "").toString());
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
            if (loadPluginForTrack (trackId, d, nullptr) && ! (bool) v.getProperty ("quiet", false))
                onOpenPlugin (v);
            return;
        }
}

void MainView::onTrackFx (const juce::var& v)
{
    const auto trackId = v["track"].toString(), uid = v["uid"].toString();
    const int idx = (int) v.getProperty ("idx", 0);
    if (! juce::isPositiveAndBelow (idx, Engine::numFx)) return;
    if (uid.isEmpty())
    {
        pluginWindows.erase (trackId + "#fx" + juce::String (idx));
        engine.setTrackFx (engine.getOrCreateSlot (trackId), idx, nullptr);
        fxDesc[trackId].erase (idx);
        sendTrackPlugins();
        saveSession();
        return;
    }
    for (const auto& d : knownPlugins.getTypes())
        if (d.createIdentifierString() == uid)
        {
            if (loadFxForTrack (trackId, idx, d, nullptr))
                onOpenFx (v);
            return;
        }
}

std::unique_ptr<juce::AudioPluginInstance> MainView::createInstance (const juce::PluginDescription& d, const juce::MemoryBlock* state, bool instrument)
{
    double sr = 44100; int bs = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice()) { sr = dev->getCurrentSampleRate(); bs = dev->getCurrentBufferSizeSamples(); }
    juce::String err;
    auto inst = formatManager.createPluginInstance (d, sr, bs, err);
    if (inst == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, sk ("Plugin sa nedá načítať"),
                                                d.name + "\n" + err);
        return nullptr;
    }
    auto layout = inst->getBusesLayout();
    if (layout.outputBuses.size() > 0) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! instrument && layout.inputBuses.size() > 0) layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (inst->checkBusesLayoutSupported (layout)) inst->setBusesLayout (layout);
    if (state != nullptr && state->getSize() > 0)
        inst->setStateInformation (state->getData(), (int) state->getSize());
    return inst;
}

bool MainView::loadPluginForTrack (const juce::String& trackId, const juce::PluginDescription& d, const juce::MemoryBlock* state)
{
    auto inst = createInstance (d, state, true);
    if (inst == nullptr) return false;
    pluginWindows.erase (trackId);
    engine.setTrackPlugin (engine.getOrCreateSlot (trackId), std::move (inst));
    trackDesc[trackId] = d;
    sendTrackPlugins();
    saveSession();
    return true;
}

bool MainView::loadFxForTrack (const juce::String& trackId, int idx, const juce::PluginDescription& d, const juce::MemoryBlock* state)
{
    auto inst = createInstance (d, state, false);
    if (inst == nullptr) return false;
    pluginWindows.erase (trackId + "#fx" + juce::String (idx));
    engine.setTrackFx (engine.getOrCreateSlot (trackId), idx, std::move (inst));
    fxDesc[trackId][idx] = d;
    sendTrackPlugins();
    saveSession();
    return true;
}

void MainView::openWindow (const juce::String& key, juce::AudioPluginInstance* p, const juce::String& title)
{
    auto it = pluginWindows.find (key);
    if (it != pluginWindows.end()) { it->second->toFront (true); return; }
    if (p == nullptr || ! p->hasEditor()) return;
    juce::Component::SafePointer<MainView> sp (this);
    pluginWindows[key] = std::make_unique<PluginWindow> (*p, title, [sp, key]
    {
        juce::MessageManager::callAsync ([sp, key] { if (sp != nullptr) sp->pluginWindows.erase (key); });
    });
}

void MainView::onOpenPlugin (const juce::var& v)
{
    const auto trackId = v["track"].toString();
    auto* p = engine.getPlugin (engine.getOrCreateSlot (trackId));
    if (p != nullptr) openWindow (trackId, p, p->getName() + "  –  " + v.getProperty ("name", "").toString());
}

void MainView::onOpenFx (const juce::var& v)
{
    const auto trackId = v["track"].toString();
    const int idx = (int) v.getProperty ("idx", 0);
    auto* p = engine.getFx (engine.getOrCreateSlot (trackId), idx);
    if (p != nullptr) openWindow (trackId + "#fx" + juce::String (idx), p, p->getName() + "  –  " + v.getProperty ("name", "").toString());
}

void MainView::sendDevices()
{
    auto o = obj();
    juce::Array<juce::var> md, ins;
    for (const auto& info : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (info.identifier))
        {
            auto m = obj();
            put (m, "id", info.identifier); put (m, "name", info.name);
            md.add (m);
        }
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const auto names = dev->getInputChannelNames();
        const auto active = dev->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i]) ins.add (names[i]);
    }
    put (o, "midi", md); put (o, "ins", ins);
    emit ("devices", o);
}

void MainView::onReady()
{
    sendPluginList();
    sendTrackPlugins();
    sendDevices();
    sendAiState();
    // posledna piesen z minuleho spustenia
    const auto last = dataDir.getChildFile ("lastsong.json");
    if (last.existsAsFile())
    {
        auto o = obj();
        put (o, "text", last.loadFileAsString());
        emit ("lastSong", o);
    }
}

void MainView::sendAiState()
{
    auto o = obj();
    put (o, "hasKey", props.getUserSettings()->getValue ("openrouterKey").isNotEmpty());
    emit ("aiState", o);
}

// poziadavka na AI cez OpenRouter (kluc ostava len v nastaveniach programu)
void MainView::onAiRequest (const juce::var& v)
{
    const auto id = v["id"].toString();
    const auto payload = v["payload"].toString();
    const auto key = props.getUserSettings()->getValue ("openrouterKey");
    juce::Component::SafePointer<MainView> sp (this);
    auto reply = [sp, id] (bool ok, int status, const juce::String& text)
    {
        juce::MessageManager::callAsync ([sp, id, ok, status, text]
        {
            if (sp == nullptr) return;
            auto o = obj();
            put (o, "id", id); put (o, "ok", ok); put (o, "status", status); put (o, "text", text);
            sp->emit ("aiResult", o);
        });
    };
    if (key.isEmpty()) { reply (false, 0, sk ("Chýba kľúč OpenRouter.")); return; }
    juce::Thread::launch ([payload, key, reply]
    {
        int status = 0;
        const juce::String headers = "Authorization: Bearer " + key + "\r\n"
                                     "Content-Type: application/json\r\n"
                                     "HTTP-Referer: https://github.com/Korgus7/Korgus-Sequencer\r\n"
                                     "X-Title: Korgus Sequencer\r\n";
        auto url = juce::URL ("https://openrouter.ai/api/v1/chat/completions").withPOSTData (payload);
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                 .withExtraHeaders (headers)
                                                 .withConnectionTimeoutMs (120000)
                                                 .withStatusCode (&status));
        if (stream == nullptr) { reply (false, status, sk ("Nepodarilo sa spojiť s OpenRouter. Skontroluj internet.")); return; }
        const auto text = stream->readEntireStreamAsString();
        reply (status >= 200 && status < 300, status, text);
    });
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
    auto entry = [&o] (const juce::String& id) -> juce::var
    {
        if (! o.hasProperty (id)) put (o, id, obj());
        return o[juce::Identifier (id)];
    };
    for (const auto& [id, d] : trackDesc)
    {
        auto t = entry (id);
        put (t, "name", d.name); put (t, "uid", d.createIdentifierString());
    }
    for (const auto& [id, m] : fxDesc)
    {
        if (m.empty()) continue;
        auto t = entry (id);
        juce::Array<juce::var> fx;
        for (int i = 0; i < Engine::numFx; ++i)
        {
            auto it = m.find (i);
            if (it == m.end()) { fx.add (juce::var()); continue; }
            auto f = obj();
            put (f, "name", it->second.name); put (f, "uid", it->second.createIdentifierString());
            fx.add (f);
        }
        put (t, "fx", fx);
    }
    emit ("trackPlugins", o);
}

//==============================================================================
juce::var MainView::collectPluginStates()
{
    auto o = obj();
    auto stateOf = [] (juce::AudioPluginInstance* p, const juce::PluginDescription& d)
    {
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        auto t = obj();
        if (auto x = d.createXml()) put (t, "desc", x->toString (juce::XmlElement::TextFormat().singleLine()));
        put (t, "state", mb.toBase64Encoding());
        return t;
    };
    auto entry = [&o] (const juce::String& id) -> juce::var
    {
        if (! o.hasProperty (id)) put (o, id, obj());
        return o[juce::Identifier (id)];
    };
    for (const auto& [id, d] : trackDesc)
    {
        auto* p = engine.getPlugin (engine.getOrCreateSlot (id));
        if (p == nullptr) continue;
        auto t = entry (id);
        auto st = stateOf (p, d);
        put (t, "desc", st["desc"]); put (t, "state", st["state"]);
    }
    for (const auto& [id, m] : fxDesc)
    {
        juce::Array<juce::var> fx;
        for (const auto& [idx, d] : m)
            if (auto* p = engine.getFx (engine.getOrCreateSlot (id), idx))
            {
                auto st = stateOf (p, d);
                put (st, "idx", idx);
                fx.add (st);
            }
        if (! fx.isEmpty()) { auto t = entry (id); put (t, "fx", fx); }
    }
    return o;
}

void MainView::restorePluginStates (const juce::var& vst)
{
    auto load = [this] (const juce::var& t, juce::PluginDescription& desc, juce::MemoryBlock& mb)
    {
        const auto xml = juce::parseXML (t["desc"].toString());
        if (xml == nullptr || ! desc.loadFromXml (*xml)) return false;
        if (juce::File::isAbsolutePath (desc.fileOrIdentifier) && ! juce::File (desc.fileOrIdentifier).exists())
            for (const auto& k : knownPlugins.getTypes())   // plugin sa presunul (napr. vedla programu)
                if (k.uniqueId == desc.uniqueId && k.name == desc.name && k.pluginFormatName == desc.pluginFormatName) { desc = k; break; }
        mb.fromBase64Encoding (t["state"].toString());
        return true;
    };
    if (auto* d = vst.getDynamicObject())
    {
        for (const auto& prop : d->getProperties())
        {
            const auto trackId = prop.name.toString();
            juce::PluginDescription desc;
            juce::MemoryBlock mb;
            if (prop.value.hasProperty ("desc") && load (prop.value, desc, mb))
                loadPluginForTrack (trackId, desc, &mb);
            if (const auto* fx = prop.value["fx"].getArray())
                for (const auto& f : *fx)
                {
                    juce::PluginDescription fd;
                    juce::MemoryBlock fm;
                    if (load (f, fd, fm)) loadFxForTrack (trackId, (int) f.getProperty ("idx", 0), fd, &fm);
                }
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
    if (src == &deviceManager) sendDevices();
    if (src == &deviceManager)
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            if (std::abs (dev->getCurrentSampleRate() - store->getSampleRate()) > 0.5)
            {
                store->setSampleRate (dev->getCurrentSampleRate());
                if (! lastSong.isVoid()) onSong (lastSong);
            }
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
        if (auto* d = j.getDynamicObject())
        {
            d->setProperty ("vst", collectPluginStates());
            auto ad = obj();
            if (const auto* tracks = j["tracks"].getArray())
                for (const auto& t : *tracks)
                {
                    juce::Array<juce::var> ids;
                    if (const auto* parts = t["parts"].getArray())
                        for (const auto& p : *parts) ids.add (p["aid"]);
                    if (auto* sm = t["smp"].getDynamicObject())
                        for (const auto& prop : sm->getProperties()) ids.add (prop.value);
                        for (const auto& idv : ids)
                        {
                            const auto id = idv.toString();
                            if (id.isEmpty() || ad.hasProperty (id)) continue;
                            const auto f = store->fileFor (id);
                            juce::MemoryBlock mb;
                            if (! f.loadFileAsData (mb)) continue;
                            auto a = obj();
                            put (a, "name", f.getFileName());
                            put (a, "mime", f.hasFileExtension ("mp3") ? "audio/mpeg" : "audio/" + f.getFileExtension().substring (1));
                            put (a, "b64", juce::Base64::toBase64 (mb.getData(), mb.getSize()));
                            put (ad, id, a);
                        }
                }
            if (auto* adObj = ad.getDynamicObject(); adObj != nullptr && ! adObj->getProperties().isEmpty())
                d->setProperty ("audioData", ad);
        }
        const auto txt = juce::JSON::toString (j, true);
        data.append (txt.toRawUTF8(), txt.getNumBytesAsUTF8());
    }
    else if (v.hasProperty ("b64"))
    {
        juce::MemoryOutputStream mos (data, false);
        juce::Base64::convertFromBase64 (mos, v["b64"].toString());
    }
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
                              for (auto key : { "audioData", "sampleData" })
                              if (auto* ad = j[key].getDynamicObject())
                                  for (const auto& prop : ad->getProperties())
                                  {
                                      juce::MemoryOutputStream mos;
                                      juce::Base64::convertFromBase64 (mos, prop.value["b64"].toString());
                                      sp->store->addFromData (prop.name.toString(), prop.value["name"].toString(), mos.getMemoryBlock());
                                  }
                              if (auto* d = j.getDynamicObject()) { d->removeProperty ("vst"); d->removeProperty ("audioData"); d->removeProperty ("sampleData"); }
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
    if (recOn || recFinishing)
        for (size_t i = 0; i < recTracks.size(); ++i) engine.drainRecording ((int) i, recTracks[i].l, recTracks[i].r);
    if (recFinishing && juce::Time::getMillisecondCounter() - recStopTime > 250) finishRecording();
    if (exporting.load()) return;

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

//==============================================================================
// audio stopy: import, nahravanie, export

void MainView::onImportAudio (const juce::var& v)
{
    const auto track = v["track"].toString();
    const double bar = (double) v.getProperty ("bar", 0);
    chooser = std::make_unique<juce::FileChooser> (sk ("Import audio (WAV, MP3)"), juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.mp3;*.ogg;*.flac;*.aif;*.aiff");
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp, track, bar] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (sp != nullptr && f.existsAsFile()) sp->finishImport (f, track, bar, false);
                          });
}

void MainView::onAudioChunk (const juce::var& v)
{
    const auto up = v["up"].toString();
    auto& mb = uploads[up];
    {
        juce::MemoryOutputStream mos (mb, true);
        juce::Base64::convertFromBase64 (mos, v["b64"].toString());
    }
    if (! (bool) v["last"]) return;
    auto tmp = dataDir.getChildFile ("tmp");
    tmp.createDirectory();
    auto f = tmp.getChildFile (juce::File::createLegalFileName (v["name"].toString()));
    f.replaceWithData (mb.getData(), mb.getSize());
    uploads.erase (up);
    finishImport (f, v["track"].toString(), (double) v.getProperty ("bar", 0), true);
}

void MainView::finishImport (const juce::File& f, const juce::String& track, double bar, bool deleteAfter)
{
    auto msg = obj();
    put (msg, "text", sk ("Načítavam ") + f.getFileName() + sk (" a zisťujem tempo…"));
    emit ("busy", msg);
    juce::Component::SafePointer<MainView> sp (this);
    juce::Timer::callAfterDelay (60, [sp, f, track, bar, deleteAfter]
    {
        if (sp == nullptr) return;
        const auto id = sp->store->importFile (f);
        auto o = obj();
        put (o, "track", track); put (o, "bar", bar); put (o, "name", f.getFileNameWithoutExtension());
        if (id.isEmpty()) put (o, "error", true);
        else
        {
            auto b = sp->store->get (id);
            put (o, "id", id);
            put (o, "dur", b != nullptr ? b->getNumSamples() / sp->store->getSampleRate() : 0.0);
            double bpm = 0, ph = 0;
            if (sp->store->detectTempo (id, bpm, ph)) { put (o, "bpm", bpm); put (o, "phase", ph); }
            sp->emit ("audioPeaks", sp->store->peaksFor (id));
        }
        if (deleteAfter) f.deleteFile();
        sp->emit ("audioImported", o);
    });
}

void MainView::onNeedPeaks (const juce::var& v)
{
    if (const auto* ids = v["ids"].getArray())
        for (const auto& id : *ids)
        {
            const auto pk = store->peaksFor (id.toString());
            if (pk.isObject()) emit ("audioPeaks", pk);
        }
}

void MainView::onRecAudio (const juce::var& v)
{
    if ((bool) v["on"])
    {
        if (recFinishing) finishRecording();
        recTracks.clear();
        std::vector<Engine::RecSpec> specs;
        if (const auto* arr = v["tracks"].getArray())
            for (const auto& t : *arr)
            {
                RecTrack rt;
                rt.id = t["track"].toString();
                const auto inp = t.getProperty ("inp", "0").toString();
                rt.a = inp.upToFirstOccurrenceOf ("+", false, false).getIntValue();
                rt.b = inp.contains ("+") ? inp.fromFirstOccurrenceOf ("+", false, false).getIntValue() : -1;
                specs.push_back ({ rt.a, rt.b });
                recTracks.push_back (std::move (rt));
            }
        if (specs.empty()) return;
        engine.startRecording (specs);
        recOn = true;
    }
    else if (recOn)
    {
        engine.stopRecording();
        recOn = false;
        recFinishing = true;
        recStopTime = juce::Time::getMillisecondCounter();
    }
}

void MainView::finishRecording()
{
    recFinishing = false;
    const double sr = engine.getSampleRate();
    int lat = 0;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        lat = juce::jmax (0, dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples());
    for (size_t i = 0; i < recTracks.size(); ++i)
    {
        auto& rt = recTracks[i];
        engine.drainRecording ((int) i, rt.l, rt.r);
        if (! engine.hasRecStarted() || (double) rt.l.size() <= lat + sr * 0.1) continue;
        const int n = (int) rt.l.size() - lat;
        const bool stereo = rt.b >= 0 && rt.r.size() == rt.l.size();
        const auto id = store->addRecording (rt.l.data() + lat, stereo ? rt.r.data() + lat : nullptr, n, sr);
        if (id.isEmpty()) continue;
        emit ("audioPeaks", store->peaksFor (id));
        auto o = obj();
        put (o, "track", rt.id); put (o, "id", id);
        put (o, "start", engine.getRecStartStep()); put (o, "dur", n / sr);
        emit ("audioRec", o);
    }
    recTracks.clear();
}

namespace
{
    struct ExportJob : juce::ThreadWithProgressWindow
    {
        ExportJob (Engine& e, std::unique_ptr<SongData> s, double endStep, std::unique_ptr<juce::AudioFormatWriter> w,
                   juce::File f, std::function<void (bool)> onDone)
            : ThreadWithProgressWindow (sk ("Export WAV – ") + f.getFileName(), true, true),
              engine (e), sd (std::move (s)), end (endStep), writer (std::move (w)), file (f), done (std::move (onDone)) {}

        void run() override
        {
            ok = engine.renderOffline (*sd, end, 3.0, *writer, [this] (double p) { setProgress (p); return ! threadShouldExit(); });
        }
        void threadComplete (bool cancelled) override
        {
            writer.reset();
            const bool good = ok && ! cancelled;
            if (! good) file.deleteFile();
            if (done) done (good);
            delete this;
        }

        Engine& engine;
        std::unique_ptr<SongData> sd;
        double end;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::File file;
        std::function<void (bool)> done;
        bool ok = false;
    };
}

void MainView::onExportWav (const juce::var& v)
{
    if (exporting.load() || lastSong.isVoid()) return;
    const double end = juce::jmax (16.0, (double) v.getProperty ("end", 64));
    chooser = std::make_unique<juce::FileChooser> (sk ("Export WAV"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("piesen.wav"), "*.wav");
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp, end] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (sp == nullptr || f == juce::File()) return;
                              f = f.withFileExtension ("wav");
                              auto sd = sp->parseCurrent (sp->lastSong);
                              if (sd == nullptr) return;
                              f.deleteFile();
                              std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
                              if (os == nullptr) return;
                              juce::WavAudioFormat wav;
                              auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                                   .withSampleRate (sp->engine.getSampleRate())
                                                                   .withNumChannels (2).withBitsPerSample (24));
                              if (w == nullptr) return;
                              sp->engine.stop();
                              sp->exporting = true;
                              auto* job = new ExportJob (sp->engine, std::move (sd), end, std::move (w), f, [sp, f] (bool good)
                              {
                                  if (sp == nullptr) return;
                                  sp->exporting = false;
                                  sp->lastSentPos = -1;
                                  auto o = obj();
                                  put (o, "ok", good); put (o, "name", f.getFullPathName());
                                  sp->emit ("exported", o);
                              });
                              job->launchThread();
                          });
}
