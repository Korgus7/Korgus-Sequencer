#include "MainView.h"
#include "BuiltinFx.h"
#include "Autotune.h"
#include "Mp3Export.h"
#include "BinaryData.h"
#include "SongParse.h"
#include "Melody.h"
#include "Separator.h"

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

    store = std::make_unique<AudioStore> (dataDir.getChildFile ("audio"));

    engine.onMidiIn = [this] (int pitch, float vel, bool on, double pos)
    {
        auto o = obj();
        put (o, "pitch", pitch); put (o, "vel", vel); put (o, "on", on); put (o, "pos", pos);
        emit ("midiIn", o);
    };

    loadSession();

    web = std::make_unique<juce::WebBrowserComponent> (makeOptions());
    web->setWantsKeyboardFocus (true);
    PluginWindow::hostKey = [this] (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::spaceKey) { auto o = obj(); put (o, "code", juce::String ("Space")); emit ("hostKey", o); }
    };
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
    PluginWindow::hostKey = nullptr;
    pluginWindows.clear();
    for (auto& [k, w] : watches) if (w->proc != nullptr) w->proc->removeListener (w.get());
    watches.clear();
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
        .withEventListener ("getParams",   [this] (const juce::var& v)
                            {
                                const auto track = v["track"].toString();
                                const int which = (int) v.getProperty ("w", 0);
                                const int slot = engine.getOrCreateSlot (track);
                                auto* p = which == 0 ? engine.getPlugin (slot) : engine.getFx (slot, which - 1);
                                juce::Array<juce::var> arr;
                                if (p != nullptr)
                                {
                                    const auto& params = p->getParameters();
                                    for (int i = 0; i < params.size() && arr.size() < 400; ++i)
                                    {
                                        auto* pr = params[i];
                                        if (! pr->isAutomatable()) continue;
                                        const auto nm = pr->getName (60);
                                        if (nm.isEmpty() || nm.startsWithIgnoreCase ("MIDI CC")) continue;
                                        auto o = obj();
                                        put (o, "i", i); put (o, "name", nm); put (o, "v", pr->getValue());
                                        arr.add (o);
                                    }
                                }
                                auto o = obj();
                                put (o, "track", track); put (o, "w", which); put (o, "plugin", p != nullptr ? p->getName() : juce::String());
                                put (o, "list", arr);
                                emit ("params", o);
                            })
        .withEventListener ("importMidi",  [this] (const juce::var&)
                            {
                                chooser = std::make_unique<juce::FileChooser> (sk ("Import MIDI"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
                                juce::Component::SafePointer<MainView> sp (this);
                                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                      [sp] (const juce::FileChooser& fc)
                                                      {
                                                          const auto f = fc.getResult();
                                                          if (sp != nullptr && f.existsAsFile()) sp->emit ("midiImported", readMidiFile (f));
                                                      });
                            })
        .withEventListener ("exportStems", [this] (const juce::var& v) { onExportStems (v); })
        .withEventListener ("autoRec",     [this] (const juce::var& v) { autoRecOn = (bool) v["on"]; })
        .withEventListener ("separate",    [this] (const juce::var& v) { onSeparate (v); })
        .withEventListener ("autotune",    [this] (const juce::var& v) { onAutotune (v); })
        .withEventListener ("bounce",      [this] (const juce::var& v) { onBounce (v); })
        .withEventListener ("exportMp3",   [this] (const juce::var& v) { onExportMp3 (v); })
        .withEventListener ("clipPeak",    [this] (const juce::var& v)
                            {
                                auto b = store->get (v["id"].toString());
                                float pk = 0;
                                if (b != nullptr)
                                {
                                    const double sr = store->getSampleRate();
                                    const int a0 = juce::jlimit (0, b->getNumSamples(), (int) ((double) v.getProperty ("off", 0.0) * sr));
                                    const int a1 = juce::jlimit (a0, b->getNumSamples(), a0 + (int) ((double) v.getProperty ("dur", 0.0) * sr));
                                    for (int c = 0; c < b->getNumChannels(); ++c) pk = juce::jmax (pk, b->getMagnitude (c, a0, a1 - a0));
                                }
                                auto o = obj(); put (o, "req", v["req"]); put (o, "peak", pk);
                                emit ("clipResult", o);
                            })
        .withEventListener ("reverseAudio",[this] (const juce::var& v)
                            {
                                const auto nid = store->addReversed (v["id"].toString());
                                auto o = obj(); put (o, "req", v["req"]);
                                if (nid.isNotEmpty())
                                {
                                    put (o, "id", nid);
                                    if (auto b = store->get (nid)) put (o, "full", b->getNumSamples() / store->getSampleRate());
                                    emit ("audioPeaks", store->peaksFor (nid));
                                }
                                emit ("clipResult", o);
                            })
        .withEventListener ("melody",      [this] (const juce::var& v)
                            {
                                const auto req = v["req"].toString();
                                auto buf = store->get (v["id"].toString());
                                const double sr = store->getSampleRate(), off = (double) v.getProperty ("off", 0.0), dur = (double) v.getProperty ("dur", 0.0);
                                juce::Component::SafePointer<MainView> sp (this);
                                juce::Thread::launch ([sp, buf, sr, off, dur, req]
                                {
                                    juce::Array<juce::var> arr;
                                    if (buf != nullptr)
                                        for (const auto& m : detectMelody (*buf, sr, off, dur))
                                        {
                                            auto o = obj();
                                            put (o, "t", m.start); put (o, "e", m.end); put (o, "p", m.pitch); put (o, "v", m.vel);
                                            arr.add (o);
                                        }
                                    juce::MessageManager::callAsync ([sp, arr, req]
                                    {
                                        if (sp == nullptr) return;
                                        auto o = obj();
                                        put (o, "req", req); put (o, "notes", arr);
                                        sp->emit ("melodyNotes", o);
                                    });
                                });
                            })
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
        .withEventListener ("ready",       [this] (const juce::var&)
                            {
                                onReady();
                                juce::Component::SafePointer<MainView> sp (this);
                                for (int ms : { 50, 400, 1500 })
                                    juce::Timer::callAfterDelay (ms, [sp] { if (sp != nullptr && sp->isShowing() && juce::Process::isForegroundProcess()) sp->focusWeb(); });
                            });
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
                         [this] (const juce::String& id, double alpha, int semi) { return clipBuffer (id, alpha, semi); });
}

void MainView::onSong (const juce::var& v)
{
    if (&v != &lastSong)   // var sa nesmie priradit sam do seba
    {
        const juce::var parsed = v.isString() ? juce::JSON::parse (v.toString()) : v;   // JS posiela JSON text
        lastSong = parsed;
    }
    auto sd = parseCurrent (lastSong);
    if (sd == nullptr) return;
    engine.setSong (std::move (sd));
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
        unwatchPlugin (trackId, 0);
        engine.setTrackPlugin (engine.getOrCreateSlot (trackId), nullptr);
        trackDesc.erase (trackId);
        sendTrackPlugins();
        saveSession();
        return;
    }
    for (const auto& d : allPluginTypes())
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
        unwatchPlugin (trackId, idx + 1);
        engine.setTrackFx (engine.getOrCreateSlot (trackId), idx, nullptr);
        fxDesc[trackId].erase (idx);
        sendTrackPlugins();
        saveSession();
        return;
    }
    for (const auto& d : allPluginTypes())
        if (d.createIdentifierString() == uid)
        {
            if (loadFxForTrack (trackId, idx, d, nullptr) && ! (bool) v.getProperty ("quiet", false))
                onOpenFx (v);
            return;
        }
}

std::unique_ptr<juce::AudioPluginInstance> MainView::createInstance (const juce::PluginDescription& d, const juce::MemoryBlock* state, bool instrument)
{
    double sr = 44100; int bs = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice()) { sr = dev->getCurrentSampleRate(); bs = dev->getCurrentBufferSizeSamples(); }
    juce::String err;
    auto inst = BuiltinFx::isBuiltin (d) ? BuiltinFx::create (d) : formatManager.createPluginInstance (d, sr, bs, err);
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
    unwatchPlugin (trackId, 0);
    auto* raw = inst.get();
    engine.setTrackPlugin (engine.getOrCreateSlot (trackId), std::move (inst));
    watchPlugin (trackId, 0, raw);
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
    unwatchPlugin (trackId, idx + 1);
    auto* raw = inst.get();
    engine.setTrackFx (engine.getOrCreateSlot (trackId), idx, std::move (inst));
    watchPlugin (trackId, idx + 1, raw);
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
    const auto scaleKey = "pluginScale_" + p->getName().removeCharacters (" \t:/\\");
    const float scale = (float) props.getUserSettings()->getDoubleValue (scaleKey, 1.0);
    pluginWindows[key] = std::make_unique<PluginWindow> (*p, title, [sp, key]
    {
        juce::MessageManager::callAsync ([sp, key] { if (sp != nullptr) sp->pluginWindows.erase (key); });
    }, scale, [sp, scaleKey] (float s)
    {
        if (sp == nullptr) return;
        sp->props.getUserSettings()->setValue (scaleKey, (double) s);
        sp->props.saveIfNeeded();
    });
}

void MainView::onOpenPlugin (const juce::var& v)
{
    const auto trackId = v["track"].toString();
    auto* p = engine.getPlugin (engine.getOrCreateSlot (trackId));
    if (p != nullptr) openWindow (trackId, p, p->getName() + sk ("  –  ") + v.getProperty ("name", "").toString());
}

void MainView::onOpenFx (const juce::var& v)
{
    const auto trackId = v["track"].toString();
    const int idx = (int) v.getProperty ("idx", 0);
    auto* p = engine.getFx (engine.getOrCreateSlot (trackId), idx);
    if (p != nullptr) openWindow (trackId + "#fx" + juce::String (idx), p, p->getName() + sk ("  –  ") + v.getProperty ("name", "").toString());
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

void MainView::bootStatus (const juce::String& text, double progress)
{
    auto o = obj();
    put (o, "text", text); put (o, "p", progress);
    emit ("boot", o);
}

// start po nacitani rozhrania: po krokoch, aby okno nezamrzlo a bolo vidiet, co sa deje
void MainView::onReady()
{
    if (booted)
    {
        sendPluginList(); sendTrackPlugins(); sendDevices(); sendAiState();
        return;
    }
    booted = true;
    bootSteps.clear();
    auto step = [this] (juce::String text, std::function<void()> fn) { bootSteps.push_back ({ std::move (text), std::move (fn) }); };

    step (sk ("Otváram poslednú pieseň…"), [this]
    {
        const auto last = dataDir.getChildFile ("lastsong.json");
        if (last.existsAsFile())
        {
            auto o = obj();
            put (o, "text", last.loadFileAsString());
            emit ("lastSong", o);
        }
    });
    step (sk ("Otváram zvukovú kartu…"), [this]
    {
        std::unique_ptr<juce::XmlElement> saved (props.getUserSettings()->getXmlValue ("audioDevice"));
        deviceManager.initialise (2, 2, saved.get(), true);
        deviceManager.addChangeListener (this);
        deviceManager.addAudioCallback (&engine);
        deviceManager.addMidiInputDeviceCallback ({}, &engine);
        if (auto* dev = deviceManager.getCurrentAudioDevice())
        {
            store->setSampleRate (dev->getCurrentSampleRate());
            bootStatus (sk ("Zvuková karta: ") + dev->getName(), -1);
        }
    });
    step (sk ("Hľadám pluginy vedľa programu…"), [this]
    {
    // VST3 pluginy vedla programu (napr. Korgus.vst3) sa pridaju samy; po aktualizacii suboru sa nacitaju znova
        {
            // stara rozbalena kopia z nastaveni uz netreba
            const auto oldDir = dataDir.getChildFile ("Plugins");
            for (const auto& d : knownPlugins.getTypes())
                if (juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier).isAChildOf (oldDir))
                    knownPlugins.removeType (d);
            oldDir.deleteRecursively();
    
            // pluginy vedla programu: len ak rovnaky plugin nie je uz v priecinku VST (aby nebol v zozname dvakrat)
        const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto inExeDir = [&exeDir] (const juce::PluginDescription& d)
        {
            return juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier).getParentDirectory() == exeDir;
        };
        auto elsewhere = [&] (const juce::PluginDescription& d)
        {
            for (const auto& k : knownPlugins.getTypes())
                if (! inExeDir (k) && k.name == d.name && k.uniqueId == d.uniqueId && k.pluginFormatName == d.pluginFormatName) return true;
            return false;
        };
        for (const auto& d : knownPlugins.getTypes())
            if (inExeDir (d) && elsewhere (d)) knownPlugins.removeType (d);

        const auto files = exeDir.findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3");
        for (auto* fmt : formatManager.getFormats())
        {
            if (fmt->getName() != "VST3") continue;
            for (const auto& f : files)
            {
                bool sameName = false;   // rovnaky subor uz je v priecinku VST - netreba ho ani otvarat
                for (const auto& k : knownPlugins.getTypes())
                    if (! inExeDir (k) && juce::File::isAbsolutePath (k.fileOrIdentifier)
                        && juce::File (k.fileOrIdentifier).getFileName().equalsIgnoreCase (f.getFileName())) { sameName = true; break; }
                if (sameName || knownPlugins.isListingUpToDate (f.getFullPathName(), *fmt)) continue;
                juce::OwnedArray<juce::PluginDescription> found;
                fmt->findAllTypesForFile (found, f.getFullPathName());
                for (auto* d : found)
                    if (! elsewhere (*d)) knownPlugins.addType (*d);
            }
        }
        }
        sendPluginList(); sendDevices(); sendAiState();
    });
    // pluginy stop z minuleho spustenia - po jednom
    if (! sessionApplied && pendingSession.isObject())
    {
        sessionApplied = true;
        if (auto* d = pendingSession.getDynamicObject())
            for (const auto& prop : d->getProperties())
            {
                auto one = obj();
                put (one, prop.name, prop.value);
                juce::String name;
                if (auto x = juce::parseXML (prop.value["desc"].toString())) name = x->getStringAttribute ("name");
                if (name.isEmpty()) name = sk ("efekty stopy");
                step (sk ("Načítavam nástroj: ") + name + sk ("…"), [this, one] { restorePluginStates (one); });
            }
        pendingSession = juce::var();
    }
    step (sk ("Načítavam zvukové súbory…"), [this]
    {
        // id zvukov z poslednej piesne (subor + to, co poslalo rozhranie)
        juce::StringArray ids;
        auto collect = [&ids] (const juce::var& j)
        {
            const auto song = j.isString() ? juce::JSON::parse (j.toString()) : j;
            if (const auto* tracks = song["tracks"].getArray())
                for (const auto& t : *tracks)
                {
                    if (const auto* parts = t["parts"].getArray())
                        for (const auto& p : *parts) if (p["aid"].toString().isNotEmpty()) ids.addIfNotAlreadyThere (p["aid"].toString());
                    if (auto* sm = t["smp"].getDynamicObject())
                        for (const auto& prop : sm->getProperties()) ids.addIfNotAlreadyThere (prop.value.toString());
                }
        };
        collect (lastSong);
        const auto last = dataDir.getChildFile ("lastsong.json");
        if (last.existsAsFile()) collect (juce::var (last.loadFileAsString()));
        int k = 0;
        for (const auto& id : ids)
        {
            ++k;
            if (store->peek (id) != nullptr) continue;
            bootStatus (sk ("Načítavam zvuk ") + juce::String (k) + " / " + juce::String (ids.size()) + sk ("…"), -1);
            store->get (id);
        }
    });
    step (sk ("Hotovo"), [this]
    {
        booting = false;
        if (! lastSong.isVoid()) onSong (lastSong);
        sendTrackPlugins();
        emit ("bootDone", obj());
    });
    bootIndex = 0;
    runBootStep();
}

void MainView::runBootStep()
{
    if (bootIndex >= bootSteps.size()) return;
    auto& st = bootSteps[bootIndex];
    bootStatus (st.first, (double) bootIndex / (double) bootSteps.size());
    juce::Component::SafePointer<MainView> sp (this);
    // kratka pauza, aby sa text stihol zobrazit
    juce::Timer::callAfterDelay (30, [sp]
    {
        if (sp == nullptr) return;
        auto fn = sp->bootSteps[sp->bootIndex].second;
        fn();
        ++sp->bootIndex;
        sp->runBootStep();
    });
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

juce::Array<juce::PluginDescription> MainView::allPluginTypes() const
{
    auto list = BuiltinFx::descriptions();
    for (const auto& d : knownPlugins.getTypes()) list.add (d);
    return list;
}

void MainView::sendPluginList()
{
    auto list = allPluginTypes();
    std::sort (list.begin(), list.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        if (a.isInstrument != b.isInstrument) return a.isInstrument;
        const bool ba = BuiltinFx::isBuiltin (a), bb = BuiltinFx::isBuiltin (b);
        if (ba != bb) return ba;
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
    if (recOn) sendLivePeaks();
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
        {
            const juce::var sj = lastSong.isString() ? juce::JSON::parse (lastSong.toString()) : lastSong;
            recLoop.on = (bool) sj.getProperty ("loopOn", false);
            recLoop.L = (double) sj.getProperty ("locL", 0.0);
            recLoop.R = (double) sj.getProperty ("locR", 0.0);
            recLoop.tempo = juce::jlimit (20.0, 400.0, (double) sj.getProperty ("tempo", 120.0));
        }
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
        // nahravanie v cykle: kazdy prechod je jeden take (posun v subore v sekundach od zaciatku cyklu)
        const double st0 = engine.getRecStartStep();
        if (recLoop.on && recLoop.R > recLoop.L && st0 < recLoop.R)
        {
            const double secStep = 60.0 / (recLoop.tempo * 4.0), loopSec = (recLoop.R - recLoop.L) * secStep;
            const double firstSec = (recLoop.R - st0) * secStep, total = n / sr;
            if (total > firstSec + loopSec * 0.5)
            {
                juce::Array<juce::var> takes;
                if (st0 <= recLoop.L + 0.5 || firstSec >= loopSec * 0.5) takes.add ((recLoop.L - st0) * secStep);
                for (double t = firstSec; t + loopSec * 0.5 <= total; t += loopSec) takes.add (t);
                put (o, "takes", takes); put (o, "loopL", recLoop.L); put (o, "loopSec", loopSec);
            }
        }
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

namespace
{
    struct Mp3Job : juce::ThreadWithProgressWindow
    {
        Mp3Job (Engine& e, std::unique_ptr<SongData> s, double endStep, double lufs, int kbps, juce::File f, std::function<void (bool, juce::String)> onDone)
            : ThreadWithProgressWindow (sk ("Export MP3 – ") + f.getFileName(), true, true),
              engine (e), sd (std::move (s)), end (endStep), target (lufs), rate (kbps), file (f), done (std::move (onDone)) {}

        void run() override
        {
            const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_mp3.wav");
            tmp.deleteFile();
            const double sr = engine.getSampleRate();
            {
                std::unique_ptr<juce::OutputStream> os (tmp.createOutputStream().release());
                if (os == nullptr) { error = sk ("Nedá sa zapísať dočasný súbor."); return; }
                juce::WavAudioFormat wav;
                auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (32)
                                                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                if (w == nullptr) { error = sk ("Nedá sa zapísať dočasný súbor."); return; }
                setStatusMessage (sk ("Nahrávam pieseň…"));
                if (! engine.renderOffline (*sd, end, 3.0, *w, [this] (double p) { setProgress (p * 0.6); return ! threadShouldExit(); })) { tmp.deleteFile(); return; }
            }
            juce::AudioBuffer<float> mix;
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::FileInputStream (tmp), true));
                if (r == nullptr) { error = sk ("Dočasný súbor sa nedá prečítať."); return; }
                mix.setSize (2, (int) r->lengthInSamples);
                r->read (&mix, 0, (int) r->lengthInSamples, 0, true, true);
            }
            tmp.deleteFile();
            // ticho na konci (dozvuk 3 s) odstrihnut
            int n = mix.getNumSamples();
            while (n > (int) sr && mix.getMagnitude (0, n - 64, 64) < 1.0e-4f && mix.getMagnitude (1, n - 64, 64) < 1.0e-4f) n -= 64;
            mix.setSize (2, juce::jmin (mix.getNumSamples(), n + (int) (sr * 0.2)), true);
            setStatusMessage (sk ("Dorovnávam hlasitosť…"));
            setProgress (0.62);
            before = measureLufs (mix, sr);
            gain = normaliseLoudness (mix, sr, target, -1.0);
            setStatusMessage (sk ("Ukladám MP3…"));
            ok = writeMp3 (mix, sr, rate, file, error, [this] (double p) { setProgress (0.65 + p * 0.35); return ! threadShouldExit(); });
        }
        void threadComplete (bool cancelled) override
        {
            juce::String info;
            if (ok && target < 0) info = sk ("hlasitosť ") + juce::String (before, 1) + sk (" → ") + juce::String (target, 0) + " LUFS";
            if (done) done (ok && ! cancelled, ok ? info : error);
            delete this;
        }

        Engine& engine;
        std::unique_ptr<SongData> sd;
        double end, target;
        int rate;
        juce::File file;
        std::function<void (bool, juce::String)> done;
        juce::String error;
        double before = 0, gain = 0;
        bool ok = false;
    };
}

void MainView::onExportMp3 (const juce::var& v)
{
    if (exporting.load() || lastSong.isVoid()) return;
    const double end = juce::jmax (16.0, (double) v.getProperty ("end", 64));
    const double lufs = (double) v.getProperty ("lufs", -14.0);
    const int kbps = juce::jlimit (128, 320, (int) v.getProperty ("kbps", 320));
    chooser = std::make_unique<juce::FileChooser> (sk ("Export MP3"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("piesen.mp3"), "*.mp3");
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp, end, lufs, kbps] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (sp == nullptr || f == juce::File()) return;
                              f = f.withFileExtension ("mp3");
                              auto sd = sp->parseCurrent (sp->lastSong);
                              if (sd == nullptr) return;
                              sp->engine.stop();
                              sp->exporting = true;
                              auto* job = new Mp3Job (sp->engine, std::move (sd), end, lufs, kbps, f, [sp, f] (bool good, juce::String info)
                              {
                                  if (sp == nullptr) return;
                                  sp->exporting = false;
                                  sp->lastSentPos = -1;
                                  auto o = obj();
                                  put (o, "ok", good); put (o, "name", f.getFullPathName()); put (o, "info", info);
                                  sp->emit ("exported", o);
                              });
                              job->launchThread();
                          });
}

//==============================================================================
// import MIDI: noty po stopach a kanaloch, v krokoch (1/16)
juce::var readMidiFile (const juce::File& f)
{
    auto res = obj();
    juce::MidiFile mf;
    juce::FileInputStream in (f);
    if (! in.openedOk() || ! mf.readFrom (in)) { put (res, "error", sk ("Súbor sa nedá prečítať ako MIDI.")); return res; }
    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) { put (res, "error", sk ("Tento typ MIDI súboru (SMPTE čas) nie je podporovaný.")); return res; }

    double tempo = 0;
    for (int t = 0; t < mf.getNumTracks() && tempo == 0; ++t)
        for (const auto* ev : *mf.getTrack (t))
            if (ev->message.isTempoMetaEvent()) { tempo = 60.0 / ev->message.getTempoSecondsPerQuarterNote(); break; }
    if (tempo > 0) put (res, "tempo", tempo);

    juce::Array<juce::var> tracks;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        juce::String name;
        std::map<int, juce::Array<juce::var>> byCh;
        for (const auto* ev : seq)
        {
            const auto& m = ev->message;
            if (m.isTrackNameEvent() && name.isEmpty()) name = m.getTextFromTextMetaEvent().trim();
            if (! m.isNoteOn()) continue;
            const double on = m.getTimeStamp();
            const double off = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : on + tpq / 4.0;
            juce::Array<juce::var> n;
            n.add (std::round (on / tpq * 4.0 * 16.0) / 16.0);
            n.add (std::round (juce::jmax (0.0, off - on) / tpq * 4.0 * 16.0) / 16.0);
            n.add (m.getNoteNumber());
            n.add (juce::jlimit (1, 127, (int) m.getVelocity()));
            byCh[m.getChannel()].add (n);
        }
        for (auto& [ch, notes] : byCh)
        {
            auto o = obj();
            put (o, "name", (name.isNotEmpty() ? name : f.getFileNameWithoutExtension()) + (byCh.size() > 1 ? " " + juce::String (ch) : juce::String()));
            put (o, "drum", ch == 10);
            put (o, "notes", notes);
            tracks.add (o);
        }
    }
    put (res, "tracks", tracks);
    return res;
}

namespace
{
    // export stop: kazda stopa zvlast do WAV (bez efektov mastra)
    struct StemsJob : juce::ThreadWithProgressWindow
    {
        StemsJob (Engine& e, std::vector<std::unique_ptr<SongData>> s, juce::StringArray n, double endStep, juce::File d,
                  std::function<void (int)> onDone)
            : ThreadWithProgressWindow (sk ("Export stôp"), true, true),
              engine (e), songs (std::move (s)), names (std::move (n)), end (endStep), dir (d), done (std::move (onDone)) {}

        void run() override
        {
            engine.bypassMaster = true;
            for (size_t k = 0; k < songs.size() && ! threadShouldExit(); ++k)
            {
                setStatusMessage (juce::String ((int) k + 1) + " / " + juce::String ((int) songs.size()) + ": " + names[(int) k]);
                const auto f = dir.getChildFile (juce::String ((int) k + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (names[(int) k]) + ".wav");
                f.deleteFile();
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
                if (os == nullptr) continue;
                juce::WavAudioFormat wav;
                auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (engine.getSampleRate()).withNumChannels (2).withBitsPerSample (24));
                if (w == nullptr) continue;
                const double base = (double) k / (double) songs.size(), part = 1.0 / (double) songs.size();
                const bool ok = engine.renderOffline (*songs[k], end, 3.0, *w, [this, base, part] (double p) { setProgress (base + p * part); return ! threadShouldExit(); });
                w.reset();
                if (ok) ++written; else f.deleteFile();
            }
            engine.bypassMaster = false;
        }
        void threadComplete (bool) override
        {
            if (done) done (written);
            delete this;
        }

        Engine& engine;
        std::vector<std::unique_ptr<SongData>> songs;
        juce::StringArray names;
        double end;
        juce::File dir;
        std::function<void (int)> done;
        int written = 0;
    };
}

namespace
{
    struct BounceJob : juce::ThreadWithProgressWindow
    {
        BounceJob (Engine& e, std::unique_ptr<SongData> s, double endStep, juce::File f, std::function<void (bool)> onDone)
            : ThreadWithProgressWindow (sk ("Bounce stopy do audia"), true, true),
              engine (e), sd (std::move (s)), end (endStep), file (f), done (std::move (onDone)) {}

        void run() override
        {
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
            if (os == nullptr) return;
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (engine.getSampleRate()).withNumChannels (2).withBitsPerSample (24));
            if (w == nullptr) return;
            engine.bypassMaster = true;
            ok = engine.renderOffline (*sd, end, 2.0, *w, [this] (double p) { setProgress (p); return ! threadShouldExit(); });
            engine.bypassMaster = false;
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (ok && ! cancelled);
            delete this;
        }

        Engine& engine;
        std::unique_ptr<SongData> sd;
        double end;
        juce::File file;
        std::function<void (bool)> done;
        bool ok = false;
    };
}

void MainView::onBounce (const juce::var& v)
{
    const auto req = v["req"].toString(), trackId = v["track"].toString();
    auto fail = [this, req] (const juce::String& msg) { auto o = obj(); put (o, "req", req); put (o, "error", msg); emit ("bounced", o); };
    if (exporting.load() || lastSong.isVoid()) { fail (sk ("Práve prebieha iný export.")); return; }
    const auto* tracks = lastSong["tracks"].getArray();
    int idx = -1;
    if (tracks != nullptr)
        for (int i = 0; i < tracks->size(); ++i) if ((*tracks)[i]["id"].toString() == trackId) idx = i;
    auto sd = parseCurrent (lastSong);
    if (idx < 0 || sd == nullptr || idx >= (int) sd->tracks.size()) { fail (sk ("Stopa sa nenašla.")); return; }
    for (int k = 0; k < (int) sd->tracks.size(); ++k)
    {
        auto& o = sd->tracks[(size_t) k];
        if (k != idx) { o.audible = false; o.gainL = o.gainR = 0; o.autos.clear(); o.monitor = false; }
        o.send[0] = o.send[1] = 0;
    }
    // bounce bez hlasitosti a panoramy stopy (tie zostanu na novej stope); automatizacia parametrov ostava
    auto& t = sd->tracks[(size_t) idx];
    t.audible = true; t.gainL = t.gainR = 1.0f; t.vol = 1.0f; t.pan = 0;
    t.autos.erase (std::remove_if (t.autos.begin(), t.autos.end(), [] (const AutoLane& a) { return a.type != 2; }), t.autos.end());
    const double end = juce::jmax (1.0, (double) v.getProperty ("end", 64));
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_bounce.wav");
    engine.stop();
    exporting = true;
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new BounceJob (engine, std::move (sd), end, tmp, [sp, req, tmp] (bool ok)
    {
        if (sp == nullptr) return;
        sp->exporting = false;
        auto o = obj(); put (o, "req", req);
        if (! ok) { tmp.deleteFile(); put (o, "error", juce::String ("cancel")); sp->emit ("bounced", o); return; }
        const auto id = sp->store->importFile (tmp);
        tmp.deleteFile();
        auto b = id.isNotEmpty() ? sp->store->get (id) : nullptr;
        if (b == nullptr) { put (o, "error", sk ("Výsledok sa nepodarilo uložiť.")); sp->emit ("bounced", o); return; }
        sp->emit ("audioPeaks", sp->store->peaksFor (id));
        put (o, "id", id); put (o, "dur", b->getNumSamples() / sp->store->getSampleRate());
        sp->emit ("bounced", o);
    });
    job->launchThread();
}

void MainView::onExportStems (const juce::var& v)
{
    if (exporting.load() || lastSong.isVoid()) return;
    const double end = juce::jmax (16.0, (double) v.getProperty ("end", 64));
    chooser = std::make_unique<juce::FileChooser> (sk ("Export stôp – vyber priečinok"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [sp, end] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (sp == nullptr || ! dir.isDirectory()) return;
                              std::vector<std::unique_ptr<SongData>> songs;
                              juce::StringArray names;
                              const auto* tracks = sp->lastSong["tracks"].getArray();
                              const int nt = tracks != nullptr ? tracks->size() : 0;
                              for (int i = 0; i < nt; ++i)
                              {
                                  const auto& t = tracks->getReference (i);
                                  const auto* parts = t["parts"].getArray();
                                  if ((bool) t["mute"] || parts == nullptr || parts->isEmpty()) continue;
                                  auto sd = sp->parseCurrent (sp->lastSong);
                                  if (sd == nullptr || i >= (int) sd->tracks.size()) continue;
                                  for (int k = 0; k < (int) sd->tracks.size(); ++k)
                                      if (k != i) { auto& o = sd->tracks[(size_t) k]; o.audible = false; o.gainL = o.gainR = 0; o.autos.clear(); o.monitor = false; }
                                  sd->tracks[(size_t) i].audible = true;
                                  songs.push_back (std::move (sd));
                                  names.add (t["name"].toString().isNotEmpty() ? t["name"].toString() : sk ("Stopa ") + juce::String (i + 1));
                              }
                              if (songs.empty()) return;
                              sp->engine.stop();
                              sp->exporting = true;
                              auto* job = new StemsJob (sp->engine, std::move (songs), names, end, dir, [sp, dir] (int written)
                              {
                                  if (sp == nullptr) return;
                                  sp->exporting = false;
                                  sp->lastSentPos = -1;
                                  auto o = obj();
                                  put (o, "ok", written > 0); put (o, "name", dir.getFullPathName() + "  (" + juce::String (written) + sk (" stôp)"));
                                  sp->emit ("exported", o);
                              });
                              job->launchThread();
                          });
}

//==============================================================================
void MainView::watchPlugin (const juce::String& track, int which, juce::AudioProcessor* p)
{
    unwatchPlugin (track, which);
    if (p == nullptr) return;
    auto w = std::make_unique<ParamWatch> (*this, track, which, p);
    p->addListener (w.get());
    watches[track + "#" + juce::String (which)] = std::move (w);
}

void MainView::unwatchPlugin (const juce::String& track, int which)
{
    auto it = watches.find (track + "#" + juce::String (which));
    if (it == watches.end()) return;
    if (it->second->proc != nullptr) it->second->proc->removeListener (it->second.get());
    watches.erase (it);
}

// pohyb gombika v plugine (moze prist aj z audio vlakna)
void MainView::paramTouched (const juce::String& track, int which, int index, float v, int kind)
{
    if (! autoRecOn.load()) return;
    juce::Component::SafePointer<MainView> sp (this);
    juce::MessageManager::callAsync ([sp, track, which, index, v, kind]
    {
        if (sp == nullptr) return;
        const int slot = sp->engine.getOrCreateSlot (track);
        auto* p = which == 0 ? sp->engine.getPlugin (slot) : sp->engine.getFx (slot, which - 1);
        if (p == nullptr || ! juce::isPositiveAndBelow (index, p->getParameters().size())) return;
        auto* pr = p->getParameters()[index];
        if (! pr->isAutomatable()) return;
        auto o = obj();
        put (o, "track", track); put (o, "w", which); put (o, "i", index); put (o, "kind", kind);
        put (o, "v", kind == 0 ? v : pr->getValue());
        put (o, "name", pr->getName (60)); put (o, "plugin", p->getName());
        sp->emit ("paramTouched", o);
    });
}

// tvar vlny prave nahravaneho zvuku (priebezne pre rozhranie)
void MainView::sendLivePeaks()
{
    if (! engine.hasRecStarted()) return;
    const int B = 512;
    for (auto& rt : recTracks)
    {
        juce::Array<juce::var> mn, mx;
        while (rt.pkDone + B <= rt.l.size())
        {
            float a = 1, z = -1;
            const bool st = rt.r.size() >= rt.pkDone + B;
            for (size_t j = rt.pkDone; j < rt.pkDone + B; ++j)
            {
                const float v = st ? 0.5f * (rt.l[j] + rt.r[j]) : rt.l[j];
                a = juce::jmin (a, v); z = juce::jmax (z, v);
            }
            mn.add (juce::jlimit (-127, 127, (int) std::floor (a * 127.0f)));
            mx.add (juce::jlimit (-127, 127, (int) std::ceil (z * 127.0f)));
            rt.pkDone += B;
        }
        if (mn.isEmpty()) continue;
        auto o = obj();
        put (o, "track", rt.id); put (o, "B", B); put (o, "sr", engine.getSampleRate());
        put (o, "start", engine.getRecStartStep()); put (o, "mn", mn); put (o, "mx", mx);
        emit ("recPeaks", o);
    }
}

//==============================================================================
// zvuk klipu; pri inom tempe natiahnuty na pozadi (kym nie je hotovy, klip mlci)
AudioBufPtr MainView::clipBuffer (const juce::String& id, double alpha, int semi)
{
    const bool plain = std::abs (alpha - 1.0) < 1.0e-6 && semi == 0;
    if (booting) return plain ? store->peek (id) : nullptr;
    if (plain) return store->get (id);
    if (auto b = store->peekStretched (id, alpha, semi)) return b;
    const auto key = AudioStore::stretchKey (id, alpha, semi);
    if (stretchPending.count (key) > 0) return nullptr;
    auto src = store->get (id);
    if (src == nullptr) return nullptr;
    stretchPending.insert (key);
    juce::Component::SafePointer<MainView> sp (this);
    juce::Thread::launch ([sp, src, id, alpha, semi, key]
    {
        auto b = stretchAndShift (*src, alpha, semi);
        juce::MessageManager::callAsync ([sp, b, id, alpha, semi, key]
        {
            if (sp == nullptr) return;
            sp->stretchPending.erase (key);
            sp->store->putStretched (id, alpha, semi, b);
            if (! sp->lastSong.isVoid()) { const auto copy = sp->lastSong; sp->onSong (copy); }
        });
    });
    return nullptr;
}

//==============================================================================
// oddelenie spevu od hudby (AI model v programe, bez internetu po prvom stiahnuti)
namespace
{
    const char* modelUrl = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/Kim_Vocal_2.onnx";
    const char* ortZipUrl = "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip";
   #if JUCE_WINDOWS
    const char* ortName = "onnxruntime.dll";
   #elif JUCE_MAC
    const char* ortName = "libonnxruntime.dylib";
   #else
    const char* ortName = "libonnxruntime.so";
   #endif

    juce::AudioBuffer<float> resampleTo (const juce::AudioBuffer<float>& in, double fromSr, double toSr)
    {
        if (std::abs (fromSr - toSr) < 0.5) return in;
        const double ratio = fromSr / toSr;
        const int ch = in.getNumChannels(), len = in.getNumSamples(), outLen = juce::jmax (1, (int) (len / ratio));
        juce::AudioBuffer<float> pad (ch, len + 256);
        pad.clear();
        for (int c = 0; c < ch; ++c) pad.copyFrom (c, 0, in, c, 0, len);
        juce::AudioBuffer<float> out (ch, outLen);
        for (int c = 0; c < ch; ++c) { juce::WindowedSincInterpolator ip; ip.process (ratio, pad.getReadPointer (c), out.getWritePointer (c), outLen); }
        return out;
    }

    struct SeparateJob : juce::ThreadWithProgressWindow
    {
        SeparateJob (juce::AudioBuffer<float> clip, double sr, juce::File dataAi, juce::File exeAi,
                     std::function<void (bool, juce::String, juce::AudioBuffer<float>&, juce::AudioBuffer<float>&)> onDone)
            : ThreadWithProgressWindow (juce::String::fromUTF8 ("Oddeľujem spev a hudbu"), true, true),
              mix (std::move (clip)), rate (sr), dataDir (dataAi), exeDir (exeAi), done (std::move (onDone)) {}

        juce::File find (const juce::String& name) const
        {
            if (exeDir.getChildFile (name).existsAsFile()) return exeDir.getChildFile (name);
            return dataDir.getChildFile (name);
        }
        void run() override
        {
            auto prog = [this] (double p) { setProgress (p); return ! threadShouldExit(); };
            auto model = find ("Kim_Vocal_2.onnx");
            if (! model.existsAsFile())
            {
                setStatusMessage (juce::String::fromUTF8 ("Sťahujem model (66 MB) – len prvý raz…"));
                if (! downloadFile (modelUrl, model, error, prog)) return;
            }
            auto ort = find (ortName);
            if (! ort.existsAsFile())
            {
               #if JUCE_WINDOWS
                setStatusMessage (juce::String::fromUTF8 ("Sťahujem ONNX Runtime (65 MB) – len prvý raz…"));
                const auto zip = dataDir.getChildFile ("ort.zip");
                if (! downloadFile (ortZipUrl, zip, error, prog)) return;
                juce::ZipFile zf (zip);
                for (int i = 0; i < zf.getNumEntries(); ++i)
                    if (zf.getEntry (i)->filename.endsWithIgnoreCase ("lib/onnxruntime.dll"))
                        if (std::unique_ptr<juce::InputStream> in (zf.createStreamForEntry (i)); in != nullptr)
                        {
                            ort.deleteFile();
                            juce::FileOutputStream out (ort);
                            out.writeFromInputStream (*in, -1);
                        }
                zip.deleteFile();
               #endif
                if (! ort.existsAsFile()) { error = juce::String::fromUTF8 ("Chýba ONNX Runtime: ") + ort.getFullPathName(); return; }
            }
            setStatusMessage (juce::String::fromUTF8 ("Oddeľujem spev a hudbu… (pesnička 3 min ≈ 1 – 3 min)"));
            setProgress (0.0);
            auto in44 = resampleTo (mix, rate, 44100.0);
            if (in44.getNumChannels() < 2) { in44.setSize (2, in44.getNumSamples(), true); in44.copyFrom (1, 0, in44, 0, 0, in44.getNumSamples()); }
            juce::AudioBuffer<float> voc44;
            if (! separateVocals (ort, model, in44, voc44, error, prog)) return;
            juce::AudioBuffer<float> mus44 (2, in44.getNumSamples());
            for (int c = 0; c < 2; ++c)
            {
                mus44.copyFrom (c, 0, in44, c, 0, in44.getNumSamples());
                mus44.addFrom (c, 0, voc44, c, 0, in44.getNumSamples(), -1.0f);
            }
            vocals = resampleTo (voc44, 44100.0, rate);
            music = resampleTo (mus44, 44100.0, rate);
            ok = true;
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (ok && ! cancelled, cancelled ? juce::String::fromUTF8 ("Zrušené.") : error, vocals, music);
            delete this;
        }

        juce::AudioBuffer<float> mix, vocals, music;
        double rate;
        juce::File dataDir, exeDir;
        std::function<void (bool, juce::String, juce::AudioBuffer<float>&, juce::AudioBuffer<float>&)> done;
        juce::String error;
        bool ok = false;
    };
}

namespace
{
    struct AutotuneJob : juce::ThreadWithProgressWindow
    {
        AutotuneJob (juce::AudioBuffer<float> clip, double sr, int k, int s, int sp,
                     std::function<void (bool, AutotuneResult, juce::AudioBuffer<float>&)> onDone)
            : ThreadWithProgressWindow (juce::String::fromUTF8 ("Dolaďujem spev"), true, true),
              src (std::move (clip)), rate (sr), key (k), scale (s), speed (sp), done (std::move (onDone)) {}

        void run() override
        {
            setStatusMessage (juce::String::fromUTF8 ("Dolaďujem spev…"));
            res = autotune (src, rate, key, scale, speed, out, [this] (double p) { setProgress (p); return ! threadShouldExit(); });
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (res.ok && ! cancelled, res, out);
            delete this;
        }

        juce::AudioBuffer<float> src, out;
        double rate;
        int key, scale, speed;
        AutotuneResult res;
        std::function<void (bool, AutotuneResult, juce::AudioBuffer<float>&)> done;
    };
}

void MainView::onAutotune (const juce::var& v)
{
    const auto req = v["req"].toString();
    auto src = store->get (v["id"].toString());
    auto fail = [this, req] (const juce::String& msg) { auto o = obj(); put (o, "req", req); put (o, "error", msg); emit ("clipResult", o); };
    if (src == nullptr) { fail (sk ("Zvuk klipu sa nenašiel.")); return; }
    const double sr = store->getSampleRate();
    const int a0 = juce::jlimit (0, src->getNumSamples(), (int) ((double) v.getProperty ("off", 0.0) * sr));
    const int len = juce::jlimit (0, src->getNumSamples() - a0, (int) ((double) v.getProperty ("dur", 0.0) * sr));
    if (len < (int) (sr * 0.3)) { fail (sk ("Klip je príliš krátky.")); return; }
    const int ch = juce::jlimit (1, 2, src->getNumChannels());
    juce::AudioBuffer<float> clip (ch, len);
    for (int c = 0; c < ch; ++c) clip.copyFrom (c, 0, *src, c, a0, len);
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new AutotuneJob (std::move (clip), sr, (int) v.getProperty ("key", -1), (int) v.getProperty ("scale", -1), (int) v.getProperty ("speed", 1),
        [sp, req, sr] (bool ok, AutotuneResult r, juce::AudioBuffer<float>& out)
        {
            if (sp == nullptr) return;
            auto o = obj(); put (o, "req", req);
            if (! ok) { put (o, "error", juce::String ("cancel")); sp->emit ("clipResult", o); return; }
            const auto nid = sp->store->addRecording (out.getReadPointer (0), out.getNumChannels() > 1 ? out.getReadPointer (1) : nullptr,
                                                      out.getNumSamples(), sr);
            if (nid.isEmpty()) { put (o, "error", sk ("Výsledok sa nepodarilo uložiť.")); sp->emit ("clipResult", o); return; }
            sp->emit ("audioPeaks", sp->store->peaksFor (nid));
            put (o, "id", nid); put (o, "dur", out.getNumSamples() / sr);
            put (o, "key", r.key); put (o, "scale", r.scale);
            sp->emit ("clipResult", o);
        });
    job->launchThread();
}

void MainView::onSeparate (const juce::var& v)
{
    const auto req = v["req"].toString();
    auto src = store->get (v["id"].toString());
    auto fail = [this, req] (const juce::String& msg) { auto o = obj(); put (o, "req", req); put (o, "error", msg); emit ("separated", o); };
    if (src == nullptr) { fail (sk ("Zvuk klipu sa nenašiel.")); return; }
    const double sr = store->getSampleRate();
    const int a0 = juce::jlimit (0, src->getNumSamples(), (int) ((double) v.getProperty ("off", 0.0) * sr));
    const int len = juce::jlimit (0, src->getNumSamples() - a0, (int) ((double) v.getProperty ("dur", 0.0) * sr));
    if (len < (int) sr) { fail (sk ("Klip je príliš krátky.")); return; }
    juce::AudioBuffer<float> clip (juce::jmax (2, src->getNumChannels()), len);
    for (int c = 0; c < clip.getNumChannels(); ++c) clip.copyFrom (c, 0, *src, juce::jmin (c, src->getNumChannels() - 1), a0, len);
    const auto exeAi = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("AI");
    const auto dataAi = dataDir.getChildFile ("AI");
    dataAi.createDirectory();
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new SeparateJob (std::move (clip), sr, dataAi, exeAi,
        [sp, req, sr] (bool ok, juce::String err, juce::AudioBuffer<float>& voc, juce::AudioBuffer<float>& mus)
        {
            if (sp == nullptr) return;
            auto o = obj(); put (o, "req", req);
            if (! ok) { put (o, "error", err); sp->emit ("separated", o); return; }
            const auto vid = sp->store->addRecording (voc.getReadPointer (0), voc.getReadPointer (1), voc.getNumSamples(), sr);
            const auto mid = sp->store->addRecording (mus.getReadPointer (0), mus.getReadPointer (1), mus.getNumSamples(), sr);
            if (vid.isEmpty() || mid.isEmpty()) { put (o, "error", sk ("Výsledok sa nepodarilo uložiť.")); sp->emit ("separated", o); return; }
            sp->emit ("audioPeaks", sp->store->peaksFor (vid));
            sp->emit ("audioPeaks", sp->store->peaksFor (mid));
            put (o, "voc", vid); put (o, "mus", mid); put (o, "dur", voc.getNumSamples() / sr);
            sp->emit ("separated", o);
        });
    job->launchThread();
}
