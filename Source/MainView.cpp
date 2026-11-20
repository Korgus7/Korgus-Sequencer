#include "MainView.h"
#include <juce_cryptography/juce_cryptography.h>
#include "BuiltinFx.h"
#include "Autotune.h"
#include "Denoise.h"
#include "DrumKits.h"
#include "VideoExport.h"
#include "AudioAnalysis.h"
#include "Mp3Export.h"
#include "BinaryData.h"
#include "SongParse.h"
#include "Melody.h"
#include "Separator.h"
#include "CrashGuard.h"
#include "KbdKeys.h"
#include <set>
#include <array>
#include "SoundMatch.h"

// Rozhranie moze prist aj ako samostatny subor korgus-ui.html vedla programu (aktualizacia bez noveho .exe,
// ktory by Windows mohol zablokovat). Pouzije sa len ak je novsi ako vstavane rozhranie a ma rovnake API.
namespace
{
    constexpr int kUiApi = 1;   // zvysit spolu s <meta name="korgus-api"> v index.html, ked sa zmenia udalosti medzi C++ a UI
    juce::File uiFile()    { return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("korgus-ui.html"); }
    juce::File uiVerFile() { return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("korgus-ui.ver"); }
    juce::Array<int> verNums (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::Array<int> r;
        for (const auto& t : juce::StringArray::fromTokens (v, ".", "")) r.add (t.getIntValue());
        while (r.size() < 3) r.add (0);
        return r;
    }
    bool verGreater (const juce::String& a, const juce::String& b)
    {
        const auto x = verNums (a), y = verNums (b);
        for (int i = 0; i < juce::jmax (x.size(), y.size()); ++i)
        {
            const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
            if (p != q) return p > q;
        }
        return false;
    }
    // verzia externeho rozhrania, ak sa da pouzit (inak prazdne)
    juce::String externalUiVersion()
    {
        const auto f = uiFile(), vf = uiVerFile();
        if (! f.existsAsFile() || ! vf.existsAsFile()) return {};
        const auto v = vf.loadFileAsString().trim();
        if (v.isEmpty() || ! verGreater (v, JUCE_APPLICATION_VERSION_STRING)) return {};
        juce::FileInputStream in (f);
        if (! in.openedOk()) return {};
        const auto head = in.readString().substring (0, 4000);   // hlavicka stranky
        if (! head.contains ("<meta name=\"korgus-api\" content=\"" + juce::String (kUiApi) + "\">")) return {};
        return v;
    }
}


// "Napodob ustami": nahravka z audio stopy sa tvari ako nastroj - pri note zahra nahravku preladenu podla vysky
// (zakladny ton sa zisti z nahravky), aby ju "Nauc sa zvuk" mohol porovnavat rovnako ako cudzi plugin
class ClipInstrument final : public juce::AudioPluginInstance
{
public:
    ClipInstrument (juce::AudioBuffer<float> b, double sr, int rootNote)
        : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          clip (std::move (b)), clipSr (sr), root (rootNote) {}
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "Clip"; d.pluginFormatName = "Internal"; d.isInstrument = true; }
    const juce::String getName() const override { return "Clip"; }
    void prepareToPlay (double sr, int) override { outSr = sr; pos = -1; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        b.clear();
        int start = 0;
        auto render = [&] (int from, int to)
        {
            if (pos < 0) return;
            const int n = clip.getNumSamples(), nch = clip.getNumChannels();
            for (int i = from; i < to; ++i)
            {
                const int i0 = (int) pos; if (i0 + 1 >= n) { pos = -1; return; }
                const float fr = (float) (pos - i0);
                for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
                {
                    const float* src = clip.getReadPointer (juce::jmin (c, nch - 1));
                    b.setSample (c, i, src[i0] + fr * (src[i0 + 1] - src[i0]));
                }
                pos += step;
            }
        };
        for (const auto meta : m)
        {
            const auto msg = meta.getMessage();
            render (start, meta.samplePosition); start = meta.samplePosition;
            if (msg.isNoteOn()) { pos = 0; step = clipSr / outSr * std::pow (2.0, (msg.getNoteNumber() - root) / 12.0); }
        }
        render (start, b.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    void reset() override { pos = -1; }

    // zakladny ton nahravky (autokorelacia v strede); bez vyrazneho tonu (sum, sykavky) = 60
    static int detectRoot (const juce::AudioBuffer<float>& b, double sr)
    {
        const int n = b.getNumSamples(); if (n < (int) (sr * 0.2)) return 60;
        // najhlasnejsie miesto, okno 80 ms
        const int W = (int) (sr * 0.08); int best = 0; float bestE = 0;
        for (int a = 0; a + W < n; a += W / 2) { float e = 0; for (int i = a; i < a + W; ++i) { const float x = b.getSample (0, i); e += x * x; } if (e > bestE) { bestE = e; best = a; } }
        const int lo = (int) (sr / 1000.0), hi = juce::jmin (W - 1, (int) (sr / 50.0));
        double e0 = 0; for (int i = 0; i < W; ++i) { const double x = b.getSample (0, best + i); e0 += x * x; }
        if (e0 <= 1e-9) return 60;
        int bl = -1; double bc = 0;
        for (int L = lo; L < hi; ++L)
        {
            double c = 0, e1 = 0;
            for (int i = 0; i + L < W; ++i) { const double x = b.getSample (0, best + i), y = b.getSample (0, best + i + L); c += x * y; e1 += y * y; }
            c /= std::sqrt (e0 * e1 + 1e-12);
            if (c > bc) { bc = c; bl = L; }
        }
        if (bl < 0 || bc < 0.6) return 60;
        const double f0 = sr / bl;
        return juce::jlimit (24, 96, (int) std::round (69.0 + 12.0 * std::log2 (f0 / 440.0)));
    }
private:
    juce::AudioBuffer<float> clip; double clipSr, outSr = 48000; int root; double pos = -1, step = 1;
};

struct MainView::LearnJob : juce::Thread
{
    LearnJob() : juce::Thread ("Korgus learn sound") {}
    ~LearnJob() override { stopThread (10000); }
    std::unique_ptr<juce::AudioPluginInstance> src, ins;
    juce::MemoryBlock state;
    bool allowSample = true;
    juce::String srcName;
    juce::Component::SafePointer<MainView> sp;
    SoundMatch::Result res;
    juce::uint32 last = 0;

    void run() override
    {
        res = SoundMatch::run (*src, *ins, state, allowSample, srcName, [this] (double p, const juce::String& m)
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - last > 200 || p >= 1.0)
            {
                last = now;
                auto s = sp; const int pct = juce::roundToInt (p * 100.0);
                juce::MessageManager::callAsync ([s, pct, m] { if (s != nullptr) s->learnProgressMsg (pct, m); });
            }
            return ! threadShouldExit();
        });
        auto s = sp;
        juce::MessageManager::callAsync ([s] { if (s != nullptr) s->learnFinished(); });
    }
};


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

//==============================================================================
// Hranie z klavesnice pocitaca v oknach pluginov (ako Ableton). Natívne okno pluginu dostava klavesy
// priamo od systemu, preto stav klaves zistujeme pollingom (fyzicke klavesy podla skenkodu ako e.code v JS).
struct MainView::KbdPoll : juce::Timer
{
    explicit KbdPoll (MainView& m) : mv (m) { startTimerHz (100); }
    ~KbdPoll() override { stopTimer(); releaseAll(); }

    struct Key { int sc; char ch; int semi; };   // semi = index v KEYS (A W S E D F T G Y H U J K O L P ;)
    static const std::array<Key, 17>& keys()
    {
        static const std::array<Key, 17> k { { { 0x1E, 'A', 0 }, { 0x11, 'W', 1 }, { 0x1F, 'S', 2 }, { 0x12, 'E', 3 }, { 0x20, 'D', 4 },
            { 0x21, 'F', 5 }, { 0x14, 'T', 6 }, { 0x22, 'G', 7 }, { 0x15, 'Y', 8 }, { 0x23, 'H', 9 }, { 0x16, 'U', 10 },
            { 0x24, 'J', 11 }, { 0x25, 'K', 12 }, { 0x18, 'O', 13 }, { 0x26, 'L', 14 }, { 0x19, 'P', 15 }, { 0x27, ';', 16 } } };
        return k;
    }

    MainView& mv;
    bool on = false;
    int oct = 3, vel = 100;
    juce::String track;                       // cielova stopa z JS (vybrana / armovana)
    std::set<juce::String> midiIds, drumIds;  // stopy, ktore prijmaju noty / su bicie
    std::vector<int> drumPitch;
    std::vector<juce::String> arm;            // dalsie zapnute (armovane) stopy - hraju naraz ako v Cubase
    bool jsText = false;                      // na stranke je fokus v textovom poli
    bool pageFocus = true;                    // stranka (WebView) ma fokus klavesnice - skratky spracuje sama
    struct Held { juce::String track; int slot; int pitch; std::vector<std::pair<juce::String, int>> extra; };
    std::map<int, Held> held;                 // index klavesy -> nota
    bool down[17] = {}, zD = false, xD = false, cD = false, vD = false, spD = false;

    // stopa okna pluginu v popredi ("" ak ziadne okno nie je v popredi)
    bool foregroundPlugin (juce::String& winTrack)
    {
        for (auto& [k, w] : mv.pluginWindows)
        {
            if (w == nullptr || ! w->isVisible()) continue;
            bool fg = false;
           #if JUCE_WINDOWS
            if (auto* peer = w->getPeer()) fg = KbdKeys::isForeground (peer->getNativeHandle());
           #else
            fg = w->isActiveWindow();
           #endif
            if (fg)
            {
                const auto base = k.upToFirstOccurrenceOf ("#", false, false);
                winTrack = base.startsWith ("__") ? juce::String() : base;
                return true;
            }
        }
        return false;
    }

    void releaseAll()
    {
        for (auto& [i, h] : held)
        {
            for (auto& [et, es] : h.extra) { mv.engine.liveNote (es, h.pitch, 0.0f, false); mv.recordLive (et, h.pitch, 0.0f, false, -1.0); }
            mv.engine.liveNote (h.slot, h.pitch, 0.0f, false);
            mv.recordLive (h.track, h.pitch, 0.0f, false, mv.engine.isPlaying() ? mv.engine.getPosition() : -1.0);
            auto o = obj(); put (o, "track", h.track); put (o, "key", i); put (o, "on", false);
            mv.emit ("kbdNote", o);
        }
        held.clear();
        for (auto& d : down) d = false;
        zD = xD = cD = vD = spD = false;
    }

    // skratky programu z okna pluginu (sipky, Delete, Home, Ctrl+Z...) -> stranka ako keby boli stlacene v hlavnom okne
    bool scDown[256] {};
    void forwardShortcuts (bool juceFocus)
    {
        struct Sc { int vk; const char* code; const char* key; bool needCtrl; };
        static const Sc list[] = {
            { 0x25, "ArrowLeft", "ArrowLeft", false }, { 0x27, "ArrowRight", "ArrowRight", false },
            { 0x26, "ArrowUp", "ArrowUp", false }, { 0x28, "ArrowDown", "ArrowDown", false },
            { 0x24, "Home", "Home", false }, { 0x23, "End", "End", false }, { 0x2E, "Delete", "Delete", false },
            { 0x08, "Backspace", "Backspace", false }, { 0x0D, "Enter", "Enter", false }, { 0x21, "PageUp", "PageUp", false }, { 0x22, "PageDown", "PageDown", false },
            { 0x60, "Numpad0", "0", false }, { 0x61, "Numpad1", "1", false }, { 0x62, "Numpad2", "2", false }, { 0x63, "Numpad3", "3", false },
            { 0x64, "Numpad4", "4", false }, { 0x65, "Numpad5", "5", false }, { 0x66, "Numpad6", "6", false }, { 0x67, "Numpad7", "7", false },
            { 0x68, "Numpad8", "8", false }, { 0x69, "Numpad9", "9", false }, { 0x6A, "NumpadMultiply", "*", false }, { 0x6B, "NumpadAdd", "+", false },
            { 0x6D, "NumpadSubtract", "-", false }, { 0x6E, "NumpadDecimal", ".", false }, { 0x6F, "NumpadDivide", "/", false },
            { 'Z', "KeyZ", "z", true }, { 'Y', "KeyY", "y", true }, { 'S', "KeyS", "s", true }, { 'C', "KeyC", "c", true }, { 'V', "KeyV", "v", true },
            { 'X', "KeyX", "x", true }, { 'A', "KeyA", "a", true }, { 'D', "KeyD", "d", true }, { 'E', "KeyE", "e", true }, { 'L', "KeyL", "l", true } };
        const int m = KbdKeys::mods();
        for (const auto& k : list)
        {
            const bool d = KbdKeys::isVkDown (k.vk);
            bool& was = scDown[k.vk & 0xff];
            if (d && ! was)
            {
                const bool arrow = k.vk >= 0x25 && k.vk <= 0x28;
                const bool ok = k.needCtrl ? (m & 1) != 0 : (! arrow || ! juceFocus);   // v Inspirii sipky prepinaju zvuky
                if (ok)
                {
                    auto o = obj(); put (o, "code", juce::String (k.code)); put (o, "key", juce::String (k.key));
                    put (o, "ctrl", (m & 1) != 0); put (o, "shift", (m & 2) != 0); put (o, "alt", (m & 4) != 0);
                    mv.emit ("hostKey", o);
                }
            }
            was = d;
        }
        // jednopismenove skratky (M, S, R, L, B...) bez Ctrl - okrem klaves, ktore prave hraju noty
        if ((m & 7) == 0)
            for (int vk = 'A'; vk <= 'Z'; ++vk)
            {
                const bool d = KbdKeys::isVkDown (vk);
                bool& was = ltDown[vk - 'A'];
                if (d && ! was && ! (on && noteLetter (vk)))
                {
                    auto o = obj(); put (o, "code", "Key" + juce::String::charToString ((juce::juce_wchar) vk));
                    put (o, "key", juce::String::charToString ((juce::juce_wchar) (vk + 32)));
                    put (o, "ctrl", false); put (o, "shift", false); put (o, "alt", false);
                    mv.emit ("hostKey", o);
                }
                was = d;
            }
    }
    bool ltDown[26] {};
    static bool noteLetter (int vk) { return juce::String ("AWSEDFTGYHUJKOLPZXCV").containsChar ((juce::juce_wchar) vk); }

    void sendState()
    {
        auto o = obj(); put (o, "on", on); put (o, "oct", oct); put (o, "vel", vel);
        mv.emit ("kbdState", o);
    }

    void timerCallback() override
    {
        juce::String winTrack;
        const bool fgPlug = foregroundPlugin (winTrack);
        // hlavne okno programu v popredi: noty hra tiez tento casovac (stranka ich uz nehra, aby nezneli dvakrat)
        bool fgMain = false;
        if (! fgPlug)
            if (auto* top = mv.getTopLevelComponent())
            {
               #if JUCE_WINDOWS
                if (auto* peer = top->getPeer()) fgMain = KbdKeys::isForeground (peer->getNativeHandle());
               #else
                fgMain = top->hasKeyboardFocus (true) || juce::Process::isForegroundProcess();
               #endif
            }
        const bool fg = fgPlug || fgMain;
        auto* fc = juce::Component::getCurrentlyFocusedComponent();
        const bool textFocus = (fc != nullptr && dynamic_cast<juce::TextEditor*> (fc) != nullptr) || KbdKeys::nativeTextFocus()
                               || (fgMain && jsText);   // pise sa do textoveho pola na stranke
        const bool fwd = fgPlug || (fgMain && ! pageFocus);   // stranka nema fokus (klik na listu okna) -> skratky posle tento casovac
        if (fwd && ! textFocus && juce::Process::isForegroundProcess()) forwardShortcuts (fgPlug && fc != nullptr);
        if (! fg || textFocus || KbdKeys::commandModsDown() || ! juce::Process::isForegroundProcess())
        {
            releaseAll();
            return;
        }

        // medzernik: ak JUCE okno ma fokus, spracuje ho PluginWindow::keyPressed; tu len natívny fokus pluginu
        const bool sp = KbdKeys::isScanDown (0x39, ' ');
        if (((fgPlug && fc == nullptr) || (fgMain && ! pageFocus)) && ! textFocus && sp && ! spD && PluginWindow::hostKey) PluginWindow::hostKey (juce::KeyPress (juce::KeyPress::spaceKey));
        spD = sp;

        if (! on) { releaseAll(); return; }

        // oktava / sila uderu (Z X C V)
        bool chg = false;
        const bool z = KbdKeys::isScanDown (0x2C, 'Z'), x = KbdKeys::isScanDown (0x2D, 'X'),
                   c = KbdKeys::isScanDown (0x2E, 'C'), v = KbdKeys::isScanDown (0x2F, 'V');
        if (z && ! zD) { oct = juce::jmax (0, oct - 1); chg = true; }
        if (x && ! xD) { oct = juce::jmin (7, oct + 1); chg = true; }
        if (c && ! cD) { vel = juce::jlimit (1, 127, vel - 20); chg = true; }
        if (v && ! vD) { vel = juce::jlimit (1, 127, vel + 20); chg = true; }
        zD = z; xD = x; cD = c; vD = v;
        if (chg) sendState();

        // cielova stopa: stopa okna pluginu (ak prijma noty), inak stopa vybrana v programe
        juce::String tr = winTrack;
        if (tr.isEmpty() || midiIds.count (tr) == 0) tr = track;
        if (tr.isEmpty()) { releaseAll(); return; }
        const bool drum = drumIds.count (tr) != 0;

        for (size_t n = 0; n < keys().size(); ++n)
        {
            const auto& k = keys()[n];
            const bool d = KbdKeys::isScanDown (k.sc, k.ch);
            if (d && ! down[n])
            {
                int pitch;
                if (drum) { if (k.semi >= (int) drumPitch.size()) { down[n] = d; continue; } pitch = drumPitch[(size_t) k.semi]; }
                else pitch = juce::jlimit (0, 127, (oct + 2) * 12 + k.semi);
                const int slot = mv.engine.getOrCreateSlot (tr);
                const float vf = (float) vel / 127.0f;
                mv.engine.liveNote (slot, pitch, vf, true);
                mv.recordLive (tr, pitch, vf, true, mv.engine.isPlaying() ? mv.engine.getPosition() : -1.0);
                held[(int) n] = { tr, slot, pitch, {} };
                if (! drum)
                    for (const auto& a : arm)
                        if (a != tr && midiIds.count (a) != 0 && drumIds.count (a) == 0)
                        {
                            const int es = mv.engine.getOrCreateSlot (a);
                            mv.engine.liveNote (es, pitch, vf, true);
                            mv.recordLive (a, pitch, vf, true, mv.engine.isPlaying() ? mv.engine.getPosition() : -1.0);
                            held[(int) n].extra.push_back ({ a, es });
                        }
                auto o = obj(); put (o, "track", tr); put (o, "key", (int) n); put (o, "on", true);
                put (o, "val", k.semi); put (o, "pitch", pitch); put (o, "vel", vf);
                mv.emit ("kbdNote", o);
            }
            else if (! d && down[n])
            {
                if (auto it = held.find ((int) n); it != held.end())
                {
                    for (auto& [et, es] : it->second.extra) { mv.engine.liveNote (es, it->second.pitch, 0.0f, false); mv.recordLive (et, it->second.pitch, 0.0f, false, -1.0); }
                    mv.engine.liveNote (it->second.slot, it->second.pitch, 0.0f, false);
                    mv.recordLive (it->second.track, it->second.pitch, 0.0f, false, mv.engine.isPlaying() ? mv.engine.getPosition() : -1.0);
                    auto o = obj(); put (o, "track", it->second.track); put (o, "key", (int) n); put (o, "on", false);
                    mv.emit ("kbdNote", o);
                    held.erase (it);
                }
            }
            down[n] = d;
        }
    }
};

void MainView::onKbdState (const juce::var& v)
{
    if (kbdPoll == nullptr) return;
    auto& k = *kbdPoll;
    if (v.hasProperty ("text")) { k.jsText = (bool) v["text"]; if (k.jsText) k.releaseAll(); return; }
    if (v.hasProperty ("page")) k.pageFocus = (bool) v["page"];
    // zoznam spevovych (audio) stop pre vyber vo vocoderi Inspirie
    if (const auto* a = v.getProperty ("aud", {}).getArray())
    {
        juce::StringArray names; for (const auto& x : *a) names.add (x.toString());
        const auto txt = names.joinIntoString ("\n");
        if (txt != vocTracksTxt)
        {
            vocTracksTxt = txt;
            juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_vokoder_stopy.txt").replaceWithText (txt);
        }
    }
    const bool on = (bool) v.getProperty ("on", false);
    if (! on && k.on) k.releaseAll();
    k.on = on;
    k.oct = juce::jlimit (0, 7, (int) v.getProperty ("oct", 3));
    k.vel = juce::jlimit (1, 127, (int) v.getProperty ("vel", 100));
    k.track = v.getProperty ("track", "").toString();
    k.midiIds.clear(); k.drumIds.clear(); k.drumPitch.clear();
    if (const auto* a = v.getProperty ("midi", {}).getArray()) for (const auto& x : *a) k.midiIds.insert (x.toString());
    if (const auto* a = v.getProperty ("drum", {}).getArray()) for (const auto& x : *a) k.drumIds.insert (x.toString());
    if (const auto* a = v.getProperty ("drumPitch", {}).getArray()) for (const auto& x : *a) k.drumPitch.push_back ((int) x);
    k.arm.clear();
    if (const auto* a = v.getProperty ("arm", {}).getArray()) for (const auto& x : *a) k.arm.push_back (x.toString());
}

// ------------------------------------------------------------- meranie startu
namespace
{
    struct BootLogState { double t0 = juce::Time::getMillisecondCounterHiRes(), last = t0; juce::String text; juce::File file; bool done = false; };
    BootLogState& bootLogState() { static BootLogState s; return s; }
}
void bootLogDir (const juce::File& dir)
{
    auto& s = bootLogState();
    s.file = dir.getChildFile ("start-log.txt");
    s.file.replaceWithText (s.text);
}
void bootLog (const juce::String& what)
{
    auto& s = bootLogState();
    if (s.done) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (s.text.isEmpty()) s.text << "Korgus Sequencer - start " << juce::Time::getCurrentTime().toString (true, true, true, true) << "\n"
                                 << "     cas   trvanie  krok\n";
    s.text << juce::String (now - s.t0, 0).paddedLeft (' ', 7) << " ms " << ("+" + juce::String (now - s.last, 0)).paddedLeft (' ', 7) << "  " << what << "\n";
    s.last = now;
    if (what == "hotovo") s.done = true;
    if (s.file != juce::File()) s.file.replaceWithText (s.text);
}

MainView::MainView (juce::ApplicationProperties& p) : props (p)
{
    bootLog ("MainView: zaciatok");
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    formatManager.addDefaultFormats();

    dataDir = props.getUserSettings()->getFile().getParentDirectory();
    dataDir.createDirectory();
    bootLogDir (dataDir);

    CrashGuard::install (dataDir);
    {
        const auto ci = CrashGuard::takeLastCrash();
        crashed = ci.crashed; crashSkipUid = ci.uid; crashName = ci.name; crashWhere = ci.where;
    }
    blockedPlugins.addLines (props.getUserSettings()->getValue ("blockedPlugins"));
    blockedPlugins.removeEmptyStrings();
    enabledPlugins.addLines (props.getUserSettings()->getValue ("enabledPlugins"));
    enabledPlugins.removeEmptyStrings();
    held = obj();

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
        recordLive (selTrackId, pitch, vel, on, engine.isPlaying() ? pos : -1.0);
    };

    bootLog ("nastavenia a pluginy z disku");
    loadSession();
    bootLog ("posledna relacia precitana");

    web = std::make_unique<juce::WebBrowserComponent> (makeOptions());
    bootLog ("WebView2 vytvorene");
    web->setWantsKeyboardFocus (true);
    BuiltinFx::requestFile = [this] (std::function<void (const juce::File&)> cb)
    {
        pendingFileCb = std::move (cb);
        if (auto* t = getTopLevelComponent()) t->toFront (true);
        auto o = obj(); put (o, "kind", juce::String ("sampler"));
        emit ("openBrowser", o);
    };
    PluginWindow::hostKey = [this] (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::spaceKey) { auto o = obj(); put (o, "code", juce::String ("Space")); emit ("hostKey", o); }
    };
    addAndMakeVisible (*web);
   #if JUCE_LINUX
    // na Linuxe velke stranky cez resource provider padaju - nacitame subor z disku
    auto page = dataDir.getChildFile ("ui").getChildFile ("index.html");
    page.getParentDirectory().createDirectory();
    if (externalUiVersion().isNotEmpty()) uiFile().copyFileTo (page);
    else page.replaceWithData (BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
    web->goToURL (juce::URL (page).toString (false));
   #else
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif

    kbdPoll = std::make_unique<KbdPoll> (*this);
    setSize (1320, 860);
    startTimerHz (30);
}

MainView::~MainView()
{
    if (learnJob != nullptr) { learnJob->signalThreadShouldExit(); learnJob->stopThread (10000); learnJob = nullptr; }
    stopTimer();
    kbdPoll = nullptr;
    saveSession();
    saveSettings();
    PluginWindow::hostKey = nullptr;
    BuiltinFx::requestFile = nullptr;
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
        .withEventListener ("songReset",   [this] (const juce::var&) { engine.notifyNewSong(); })
        .withEventListener ("transport",   [this] (const juce::var& v) { onTransport (v); })
        .withEventListener ("note",        [this] (const juce::var& v) { onNote (v); })
        .withEventListener ("kbdState",    [this] (const juce::var& v) { onKbdState (v); })
        .withEventListener ("selTrack",    [this] (const juce::var& v) { onSelTrack (v); })
        .withEventListener ("trackFx",     [this] (const juce::var& v) { onTrackFx (v); })
        .withEventListener ("openFx",      [this] (const juce::var& v) { onOpenFx (v); })
        .withEventListener ("trackPlugin", [this] (const juce::var& v) { onTrackPlugin (v); })
        .withEventListener ("openPlugin",  [this] (const juce::var& v) { onOpenPlugin (v); })
        .withEventListener ("showSettings",[this] (const juce::var&)   { showSettings(); })
        .withEventListener ("metro",       [this] (const juce::var& v)
        {
            engine.metroOn  = (bool) v.getProperty ("on", false);
            engine.metroVol = (float) juce::jlimit (0.0, 1.0, (double) v.getProperty ("vol", 0.8));
            engine.metroSnd = juce::jlimit (0, 3, (int) v.getProperty ("snd", 0));
            engine.metroAcc = (bool) v.getProperty ("acc", true);
            engine.metroSub = juce::jlimit (1, 4, (int) v.getProperty ("sub", 4));
        })
        .withEventListener ("metroTest",   [this] (const juce::var&)   { engine.metroTest = true; })
        .withEventListener ("showPlugins", [this] (const juce::var&)   { showPluginManager(); })
        .withEventListener ("saveFile",    [this] (const juce::var& v) { onSaveFile (v); })
        .withEventListener ("openSong",    [this] (const juce::var& v) { onOpenSong (v); })
        .withEventListener ("packProject", [this] (const juce::var& v) { onPackProject (v); })
        .withEventListener ("lang",        [this] (const juce::var& v)
        {
            const auto l = v["lang"].toString() == "en" ? juce::String ("en") : juce::String ("sk");
            appLang() = l == "en" ? 1 : 0;
            shareLangWithInspiria();
            props.getUserSettings()->setValue ("lang", l);
        })
        .withEventListener ("updateNow",   [this] (const juce::var&)   { onUpdateNow(); })
        .withEventListener ("learnSound",  [this] (const juce::var& v) { onLearnSound (v); })
        .withEventListener ("learnCancel", [this] (const juce::var&)   { if (learnJob != nullptr) learnJob->signalThreadShouldExit(); })
        .withEventListener ("learnUndo",   [this] (const juce::var&)
        {
            auto* p = learnTarget.isNotEmpty() ? engine.getPlugin (engine.getOrCreateSlot (learnTarget)) : nullptr;
            if (p != nullptr && learnPrev.getSize() > 0)
            {
                p->suspendProcessing (true);
                p->setStateInformation (learnPrev.getData(), (int) learnPrev.getSize());
                p->suspendProcessing (false);
                saveSession();
            }
        })
        .withEventListener ("crashChoice", [this] (const juce::var& v)
        {
            const auto what = v["what"].toString();
            const auto uid = crashSkipUid, name = crashName;
            crashSkipUid = {};
            if (what == "block" && uid.isNotEmpty())
            {
                blockedPlugins.add (uid + "\t" + name);
                props.getUserSettings()->setValue ("blockedPlugins", blockedPlugins.joinIntoString ("\n"));
            }
            else if (what == "retry")
            {
                auto h = held; held = obj();
                restorePluginStates (h);
            }
            sendPluginList();
            saveSession();
        })
        .withEventListener ("unblockPlugin", [this] (const juce::var& v) { unblockPlugin (v["uid"].toString()); })
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
        .withEventListener ("importMidi",  [this] (const juce::var& v)
                            {
                                if (const auto path = v["path"].toString(); juce::File::isAbsolutePath (path))
                                {
                                    if (juce::File (path).existsAsFile()) emit ("midiImported", readMidiFile (juce::File (path)));
                                    return;
                                }
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
        .withEventListener ("denoise",     [this] (const juce::var& v) { onDenoise (v); })
        .withEventListener ("drumKit",     [this] (const juce::var& v)
                            {
                                const int kit = (int) v.getProperty ("kit", 0);
                                auto o = obj(); put (o, "track", v["track"].toString()); put (o, "kit", kit);
                                juce::Array<juce::var> ids, names;
                                for (int l = 0; l < DrumKits::numLanes; ++l)
                                {
                                    juce::AudioBuffer<float> b;
                                    juce::String id;
                                    const auto fixed = "kitv3_" + juce::String (kit) + "_" + juce::String (l);
                                    if (store->fileFor (fixed).existsAsFile()) id = fixed;
                                    else if (DrumKits::make (kit, l, store->getSampleRate(), b))
                                        id = store->addRecordingAs (fixed, b.getReadPointer (0), nullptr, b.getNumSamples(), store->getSampleRate());
                                    ids.add (id); names.add (DrumKits::laneName (kit, l));
                                }
                                put (o, "ids", ids); put (o, "names", names); put (o, "name", DrumKits::kitName (kit));
                                emit ("drumKitLoaded", o);
                            })
        .withEventListener ("midiMap",     [this] (const juce::var& v)
                            {
                                juce::Array<int> keys;
                                if (auto* a = v["keys"].getArray()) for (auto& k : *a) keys.add ((int) k);
                                engine.setCcMapped (keys);
                            })
        .withEventListener ("setParam",    [this] (const juce::var& v)
                            {
                                const int slot = engine.getOrCreateSlot (v["track"].toString());
                                const int which = (int) v.getProperty ("w", 0);
                                auto* p = which == 0 ? engine.getPlugin (slot) : engine.getFx (slot, which - 1);
                                const int i = (int) v.getProperty ("i", -1);
                                if (p != nullptr && juce::isPositiveAndBelow (i, p->getParameters().size()))
                                    p->getParameters()[i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("v", 0.0)));
                            })
        .withEventListener ("instAction",  [this] (const juce::var& v)
                            {
                                // Korgus Inspire: "Prekvap ma" / "Obmena" z hlavicky stopy (prepne skryty spustac v plugine)
                                const int slot = engine.getOrCreateSlot (v["track"].toString());
                                auto* p = engine.getPlugin (slot);
                                if (p == nullptr) return;
                                if (v["what"].toString() == "vocOn")   // zapnut vocoder v Inspirii
                                {
                                    for (auto* prm : p->getParameters())
                                        if (prm->getName (64).trim() == "Vocoder") { prm->setValueNotifyingHost (1.0f); break; }
                                    return;
                                }
                                if (v["what"].toString() == "preset")   // vybrat tovarensky zvuk Inspirie podla nazvu (parameter "Zvuk")
                {
                    const auto want = v["name"].toString();
                    const int idx = (int) v.getProperty ("idx", -1);
                    for (auto* prm : p->getParameters())
                    {
                        const auto nm = prm->getName (64).trim();
                        if (nm != juce::String::fromUTF8 ("Zvuk") && nm != "Sound") continue;
                        const int n = juce::jmax (2, prm->getNumSteps());
                        int hit = -1;
                        for (int i = 0; i < n && hit < 0; ++i)
                            if (prm->getText ((float) i / (float) (n - 1), 64).trim() == want) hit = i;
                        if (hit < 0 && idx >= 0 && idx < n) hit = idx;   // nazov v inom jazyku: podla poradia
                        if (hit >= 0) prm->setValueNotifyingHost ((float) hit / (float) (n - 1));
                        break;
                    }
                    return;
                }
                const auto key = v["what"].toString() == "mut" ? juce::String ("Obmena") : juce::String ("Prekvap ma");
                                for (auto* prm : p->getParameters())
                                    if (prm->getName (64).contains (key))
                                    {
                                        prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                                        break;
                                    }
                            })
        .withEventListener ("bounce",      [this] (const juce::var& v) { onBounce (v); })
        .withEventListener ("exportMp3",   [this] (const juce::var& v) { onExportMp3 (v); })
        .withEventListener ("exportVideo", [this] (const juce::var& v) { onExportVideo (v); })
        .withEventListener ("mastering",   [this] (const juce::var& v) { onMastering (v); })
        .withEventListener ("analyze",     [this] (const juce::var& v) { onAnalyze (v); })
        .withEventListener ("refLoad",     [this] (const juce::var& v) { onRefLoad (v["path"].toString()); })
        .withEventListener ("refSet",      [this] (const juce::var& v)
                            {
                                const auto id = v["id"].toString();
                                auto b = id.isNotEmpty() ? store->get (id) : nullptr;
                                engine.setReference (b, juce::Decibels::decibelsToGain ((float) (double) v.getProperty ("gainDb", 0.0)));
                                if (b == nullptr) engine.refOn = false;
                            })
        .withEventListener ("refOn",       [this] (const juce::var& v) { engine.refOn = (bool) v.getProperty ("on", false); })
        .withEventListener ("refMatch",    [this] (const juce::var& v) { onRefMatch (v); })
        .withEventListener ("trackArp",    [this] (const juce::var& v)
                            {
                                const auto id = v["track"].toString();
                                const bool on = (bool) v.getProperty ("on", true);
                                if (setArp (id, on, nullptr) && on)
                                    if (auto* p = engine.getMidiFx (engine.getOrCreateSlot (id)))
                                        openWindow (id + "#arp", p, juce::String ("Korgus Arp") + sk ("  –  ") + v["name"].toString());
                            })
        .withEventListener ("openArp",     [this] (const juce::var& v)
                            {
                                const auto id = v["track"].toString();
                                if (auto* p = engine.getMidiFx (engine.getOrCreateSlot (id)))
                                    openWindow (id + "#arp", p, juce::String ("Korgus Arp") + sk ("  –  ") + v["name"].toString());
                            })
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
        .withEventListener ("midiReset",   [this] (const juce::var&) { engine.panic(); })
        .withEventListener ("drumKill",    [this] (const juce::var& v) { engine.killSlotVoices (engine.getOrCreateSlot (v["track"].toString())); })
        .withEventListener ("drumSet",     [this] (const juce::var& v)   // editor zvuku bicich: okamzita zmena
                            {
                                const auto& o = v["ds"];
                                auto f = [&o] (const char* k, float def) { return (float) (double) o.getProperty (k, def); };
                                DrumSet d; d.on = (bool) v.getProperty ("on", true);
                                d.tr = f ("tr", 0); d.tune = f ("tune", 0); d.pEnv = f ("pEnv", 0); d.pA = f ("pA", 0); d.pH = f ("pH", 0); d.pD = f ("pD", 0.3f); d.pVel = f ("pVel", 0);
                                d.fOn = (bool) o.getProperty ("fOn", false); d.soft = (bool) o.getProperty ("soft", true); d.fType = (int) o.getProperty ("fType", 0);
                                d.cut = f ("cut", 20000); d.res = f ("res", 0); d.fEnv = f ("fEnv", 0); d.fA = f ("fA", 0); d.fH = f ("fH", 0); d.fD = f ("fD", 0.3f);
                                d.fVel = f ("fVel", 0); d.drive = f ("drive", 0); d.punch = f ("punch", 0);
                                d.gain = f ("gain", 0); d.pan = f ("pan", 0); d.aA = f ("aA", 0); d.aH = f ("aH", 2); d.aD = f ("aD", 4); d.aVel = f ("aVel", 1);
                                engine.setDrumSetLive (engine.getOrCreateSlot (v["track"].toString()), (int) v["lane"], d);
                            })
        .withEventListener ("drumHit",     [this] (const juce::var& v)
                            {
                                engine.triggerSample (engine.getOrCreateSlot (v["track"].toString()), store->get (v["id"].toString()),
                                                      (float) (double) v.getProperty ("vel", 1.0), (int) v.getProperty ("lane", 0));
                            })
        .withEventListener ("fsList",      [this] (const juce::var& v) { onFsList (v); })
        .withEventListener ("tuner",       [this] (const juce::var& v)
                            {
                                engine.tunerCh.store ((bool) v["on"] ? juce::jmax (0, (int) v.getProperty ("ch", 0)) : -1);
                            })
        .withEventListener ("browserPick", [this] (const juce::var& v)
                            {
                                auto cb = std::move (pendingFileCb);
                                pendingFileCb = nullptr;
                                if (! cb) return;
                                if ((bool) v.getProperty ("sys", false))
                                {
                                    chooser = std::make_unique<juce::FileChooser> (sk ("Vyber zvuk pre sampler"), juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                          [cb] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) cb (fc.getResult()); });
                                    return;
                                }
                                if (const auto path = v["path"].toString(); juce::File::isAbsolutePath (path)) cb (juce::File (path));
                            })
        .withEventListener ("fsPreview",   [this] (const juce::var& v)
                            {
                                if (v["track"].toString().isEmpty()) return;
                                const int slot = engine.getOrCreateSlot (v["track"].toString());
                                const juce::File f (v["path"].toString());
                                AudioBufPtr b;
                                if (juce::File::isAbsolutePath (v["path"].toString()) && f.existsAsFile()) b = store->decodePreview (f, 20.0);
                                if (b == nullptr)   // ticho = zastavi nahlad
                                {
                                    auto quiet = std::make_shared<juce::AudioBuffer<float>> (1, 64);
                                    quiet->clear();
                                    b = quiet;
                                }
                                engine.triggerSample (slot, b, 0.9f, 99);
                            })
        .withEventListener ("pickSample",  [this] (const juce::var& v)
                            {
                                const juce::File f (v["path"].toString());
                                auto o = obj();
                                put (o, "track", v["track"].toString()); put (o, "lane", (int) v["lane"]);
                                put (o, "name", f.getFileNameWithoutExtension()); put (o, "keep", true);
                                const auto id = juce::File::isAbsolutePath (v["path"].toString()) && f.existsAsFile() ? store->importFile (f) : juce::String();
                                if (id.isEmpty()) put (o, "error", true); else put (o, "id", id);
                                emit ("sampleImported", o);
                            })
        .withEventListener ("raiseMain",   [this] (const juce::var&)
                            {
                                // klik do hlavneho okna: dopredu pred okna pluginov (napr. editor bicich nad Inspiriou)
                                // len ak je v popredi ine okno - inak by toFront vzal stranke fokus a zavrel rozbalovacie menu (select)
                                if (auto* t = dynamic_cast<juce::TopLevelWindow*> (getTopLevelComponent()))
                                    if (auto* peer = t->getPeer())
                                        if (! KbdKeys::isForeground (peer->getNativeHandle())) t->toFront (false);
                            })
        .withEventListener ("patLib",      [this] (const juce::var& v)
                            {
                                // kniznica MIDI patternov: Hudba\Korgus Patterny\{Bicie, Melódie, Hity}
                                const auto root = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Korgus Patterny");
                                const juce::String sub[3] = { "Bicie", juce::String::fromUTF8 ("Melódie"), "Hity" };
                                for (auto& d : sub) root.getChildFile (d).createDirectory();
                                if (v["open"].toString().isNotEmpty()) { root.getChildFile (v["open"].toString()).startAsProcess(); return; }
                                auto o = obj();
                                const char* keys[3] = { "drum", "mel", "hits" };
                                bool hitsEmpty = true;
                                for (int k = 0; k < 3; ++k)
                                {
                                    juce::Array<juce::var> arr;
                                    auto fs = root.getChildFile (sub[k]).findChildFiles (juce::File::findFiles, true, "*.mid;*.midi");
                                    fs.sort();
                                    for (auto& f : fs)
                                    {
                                        auto e = obj(); put (e, "n", f.getFileNameWithoutExtension()); put (e, "p", f.getFullPathName());
                                        arr.add (e);
                                    }
                                    if (k == 2) hitsEmpty = fs.isEmpty();
                                    put (o, keys[k], arr);
                                }
                                put (o, "fresh", hitsEmpty && ! root.getChildFile (".hity_v1").existsAsFile());
                                if (hitsEmpty) root.getChildFile (".hity_v1").replaceWithText ("1");
                                put (o, "fresh2", ! root.getChildFile (".hity_v2").existsAsFile());   // vlastne temy v style hitov
                                root.getChildFile (".hity_v2").replaceWithText ("1");
                                put (o, "root", root.getFullPathName());
                                emit ("patLibList", o);
                            })
        .withEventListener ("patLoad",     [this] (const juce::var& v)
                            {
                                const juce::File f (v["path"].toString());
                                auto o = obj(); put (o, "req", v["req"]); put (o, "name", f.getFileNameWithoutExtension());
                                juce::Array<juce::var> notes; int maxStep = 0;
                                juce::MidiFile mf;
                                if (juce::File::isAbsolutePath (v["path"].toString()) && f.existsAsFile())
                                    if (juce::FileInputStream in (f); in.openedOk() && mf.readFrom (in))
                                    {
                                        const int tf = mf.getTimeFormat();
                                        const double tps = tf > 0 ? tf / 4.0 : 24.0;   // tikov na sestnastinu
                                        for (int t = 0; t < mf.getNumTracks(); ++t)
                                        {
                                            juce::MidiMessageSequence seq (*mf.getTrack (t));
                                            seq.updateMatchedPairs();
                                            for (int i = 0; i < seq.getNumEvents(); ++i)
                                            {
                                                const auto& m = seq.getEventPointer (i)->message;
                                                if (! m.isNoteOn()) continue;
                                                const double t0 = m.getTimeStamp();
                                                const int off = seq.getIndexOfMatchingKeyUp (i);
                                                const double t1 = off >= 0 ? seq.getEventPointer (off)->message.getTimeStamp() : t0 + tps;
                                                const int s0 = juce::roundToInt (t0 / tps), len = juce::jmax (1, juce::roundToInt ((t1 - t0) / tps));
                                                juce::Array<juce::var> nn { s0, m.getNoteNumber(), len, (int) m.getVelocity(), m.getChannel() };
                                                notes.add (nn);
                                                maxStep = juce::jmax (maxStep, s0 + len);
                                            }
                                        }
                                    }
                                put (o, "notes", notes); put (o, "len", maxStep);
                                emit ("patLoaded", o);
                            })
        .withEventListener ("patSave",     [this] (const juce::var& v)
                            {
                                const auto root = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Korgus Patterny");
                                const auto dir = root.getChildFile (juce::File::createLegalFileName (v["folder"].toString()));
                                dir.createDirectory();
                                const auto f = dir.getChildFile (juce::File::createLegalFileName (v["name"].toString()) + ".mid");
                                const bool drum = (bool) v["drum"];
                                juce::MidiMessageSequence seq;
                                constexpr int tpq = 96, tps = tpq / 4;
                                if (auto* arr = v["notes"].getArray())
                                    for (auto& nv : *arr)
                                        if (auto* n = nv.getArray(); n != nullptr && n->size() >= 4)
                                        {
                                            const int st = (int) (*n)[0], pitch = juce::jlimit (0, 127, (int) (*n)[1]), len = juce::jmax (1, (int) (*n)[2]);
                                            const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) (*n)[3]);
                                            seq.addEvent (juce::MidiMessage::noteOn (drum ? 10 : 1, pitch, vel), st * tps);
                                            seq.addEvent (juce::MidiMessage::noteOff (drum ? 10 : 1, pitch), (st + len) * tps - 1);
                                        }
                                seq.updateMatchedPairs();
                                juce::MidiFile mf; mf.setTicksPerQuarterNote (tpq); mf.addTrack (seq);
                                f.deleteFile();
                                bool ok = false;
                                if (juce::FileOutputStream out (f); out.openedOk()) ok = mf.writeTo (out);
                                auto o = obj(); put (o, "ok", ok); put (o, "quiet", v["quiet"]); put (o, "path", f.getFullPathName());
                                emit ("patSaved", o);
                            })
        .withEventListener ("pickKit",     [this] (const juce::var& v)
                            {
                                // natiahne celu supravu z priecinka do 10 riadkov: cislo na zaciatku nazvu, potom kluc. slova, potom poradie
                                const juce::File dir (v["path"].toString());
                                auto o = obj(); put (o, "track", v["track"].toString()); put (o, "name", dir.getFileName());
                                juce::Array<juce::File> fs;
                                if (juce::File::isAbsolutePath (v["path"].toString()) && dir.isDirectory())
                                    for (auto& f : dir.findChildFiles (juce::File::findFiles, false))
                                        if (juce::StringArray { "wav", "mp3", "ogg", "flac", "aif", "aiff" }.contains (f.getFileExtension().substring (1).toLowerCase()) && ! f.isHidden()) fs.add (f);
                                fs.sort();
                                juce::File lane[10];
                                juce::Array<juce::File> rest;
                                for (auto& f : fs)
                                {
                                    const auto n = f.getFileNameWithoutExtension();
                                    const int num = n.substring (0, 2).containsOnly ("0123456789") ? n.substring (0, 2).getIntValue() : 0;
                                    if (num >= 1 && num <= 10 && lane[num - 1] == juce::File()) lane[num - 1] = f; else rest.add (f);
                                }
                                static const char* kw[10] = { "kick|bd|bassdrum|bass drum|kopak", "snare|sd|virbel", "clap|tlesk", "closed|chh|hh|hat",
                                                              "open|ohh", "low tom|tom low|floor|lt|tom1", "tom|ht|mt", "rim|stick|side", "crash|ride|cymbal|cym|cinel", "cowbell|bell|perc|shaker|tamb" };
                                static const int order[10] = { 4, 0, 1, 2, 5, 6, 7, 8, 9, 3 };   // otvoreny hi-hat a nizky tom skor nez vseobecne slova
                                for (int oi = 0; oi < 10; ++oi)
                                {
                                    const int l = order[oi];
                                    if (lane[l] != juce::File()) continue;
                                    juce::StringArray words; words.addTokens (kw[l], "|", "");
                                    for (int i = 0; i < rest.size(); ++i)
                                    {
                                        const auto low = " " + rest[i].getFileNameWithoutExtension().toLowerCase().replaceCharacters ("_-.", "   ") + " ";
                                        bool hit = false;
                                        for (auto& w : words) if (w.length() <= 3 ? low.contains (" " + w + " ") || low.contains (" " + w) : low.contains (w)) { hit = true; break; }
                                        if (hit) { lane[l] = rest[i]; rest.remove (i); break; }
                                    }
                                }
                                for (int l = 0; l < 10 && ! rest.isEmpty(); ++l) if (lane[l] == juce::File()) { lane[l] = rest[0]; rest.remove (0); }
                                juce::Array<juce::var> ids, names;
                                for (int l = 0; l < 10; ++l)
                                {
                                    const auto id = lane[l].existsAsFile() ? store->importFile (lane[l]) : juce::String();
                                    ids.add (id); names.add (id.isEmpty() ? juce::String() : lane[l].getFileNameWithoutExtension());
                                }
                                put (o, "ids", ids); put (o, "names", names);
                                emit ("kitImported", o);
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
                                if (t.length() > 10) backupMaybe (t);
                            })
        .withEventListener ("listBackups", [this] (const juce::var&) { sendBackups(); })
        .withEventListener ("loadBackup",  [this] (const juce::var& v) { loadBackup (v["name"].toString()); })
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

juce::String MainView::currentVersion()
{
    const auto ext = externalUiVersion();
    return ext.isNotEmpty() ? ext : juce::String (JUCE_APPLICATION_VERSION_STRING);
}

std::optional<juce::WebBrowserComponent::Resource> MainView::getResource (const juce::String& url)
{
    const auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty() || path == "index.html")
    {
        if (externalUiVersion().isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (uiFile().loadFileAsData (mb) && mb.getSize() > 1000)
            {
                std::vector<std::byte> data (mb.getSize());
                std::memcpy (data.data(), mb.getData(), data.size());
                return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
            }
        }
        std::vector<std::byte> data ((size_t) BinaryData::index_htmlSize);
        std::memcpy (data.data(), BinaryData::index_html, data.size());
        return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
    }
    return std::nullopt;
}

void MainView::emit (const juce::String& id, const juce::var& v)
{
    // ako WebBrowserComponent::emitEventIfBrowserIsVisible, ale JSON len v ASCII (\uXXXX) -
    // znaky s diakritikou inak mozu udalost potichu zahodit
    if (web == nullptr || ! web->isVisible()) return;
    const auto json = juce::JSON::toString (v, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::none)
                                                                          .withEncoding (juce::JSON::Encoding::ascii));
    const auto escaped = json.replace ("\\", "\\\\").replace ("'", "\\'");
    web->evaluateJavascript ("window.__JUCE__.backend.emitByBackend(" + id.quoted() + ", " + escaped.quoted ('\'') + ");");
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
    // zmazana stopa: zatvorit okna jej pluginov
    if (const auto* tr = lastSong["tracks"].getArray())
    {
        std::set<juce::String> ids;
        for (const auto& t : *tr) ids.insert (t["id"].toString());
        for (auto it = pluginWindows.begin(); it != pluginWindows.end();)
        {
            const auto base = it->first.upToFirstOccurrenceOf ("#", false, false);
            if (! base.startsWith ("__") && ids.count (base) == 0) it = pluginWindows.erase (it);
            else ++it;
        }
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
    recordLive (v["track"].toString(), (int) v["pitch"], (float) (double) v.getProperty ("vel", 0.8), (bool) v["on"],
                engine.isPlaying() ? engine.getPosition() : -1.0);
}

void MainView::recordLive (const juce::String& track, int k, float v, bool on, double pos)
{
    if (track.isEmpty()) return;
    auto& h = liveHist[track];
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    h.push_back ({ t, k, v, on, pos });
    if (h.size() > 20000 || (h.size() > 1 && t - h.front().t > 600.0))
    {
        size_t cut = 0;
        while (cut < h.size() && (h.size() - cut > 20000 || t - h[cut].t > 600.0)) ++cut;
        h.erase (h.begin(), h.begin() + (std::ptrdiff_t) cut);
    }
}

// posledna fraza: po poslednej pauze > 4 s (aspon 8 not, najviac 3 minuty); pedal predlzuje noty
juce::Array<juce::var> MainView::livePhrase (const juce::String& track, double& ppqOut)
{
    ppqOut = -1.0;
    juce::Array<juce::var> res;
    const auto it = liveHist.find (track);
    if (it == liveHist.end()) return res;
    struct N { double s, e; int k, v; double pos; };
    std::vector<N> ns; std::map<int, size_t> open; std::vector<size_t> held; bool ped = false;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (const auto& ev : it->second)
    {
        if (ev.k == -64)
        {
            ped = ev.on;
            if (! ped) { for (auto i : held) ns[i].e = ev.t; held.clear(); }
            continue;
        }
        if (ev.on && ev.v > 0.0f)
        {
            if (auto o = open.find (ev.k); o != open.end()) { ns[o->second].e = ev.t; open.erase (o); }
            open[ev.k] = ns.size();
            ns.push_back ({ ev.t, -1.0, ev.k, juce::jlimit (1, 127, (int) std::round (ev.v * 127.0f)), ev.pos });
        }
        else if (auto o = open.find (ev.k); o != open.end())
        {
            if (ped) held.push_back (o->second); else ns[o->second].e = ev.t;
            open.erase (o);
        }
    }
    for (auto& n : ns) if (n.e < 0) n.e = juce::jmax (n.s + 0.1, now);
    if (ns.empty()) return res;
    // zaciatok frazy: posledna medzera > 4 s, pricom fraza ma aspon 8 not
    size_t start = 0; double maxEnd = ns[0].e;
    std::vector<size_t> gaps;
    for (size_t j = 1; j < ns.size(); ++j) { if (ns[j].s - maxEnd > 4.0) gaps.push_back (j); maxEnd = juce::jmax (maxEnd, ns[j].e); }
    for (auto g = gaps.rbegin(); g != gaps.rend(); ++g) if (ns.size() - *g >= 8) { start = *g; break; }
    double lastEnd = 0; for (size_t j = start; j < ns.size(); ++j) lastEnd = juce::jmax (lastEnd, ns[j].e);
    while (start + 1 < ns.size() && ns[start].s < lastEnd - 180.0) ++start;
    const double t0 = ns[start].s;
    bool allPos = true;
    for (size_t j = start; j < ns.size(); ++j)
    {
        if (ns[j].pos < 0) allPos = false;
        juce::Array<juce::var> a;
        a.add (ns[j].s - t0); a.add (ns[j].e - ns[j].s); a.add (ns[j].k); a.add (ns[j].v);
        res.add (a);
    }
    if (allPos) ppqOut = ns[start].pos / 4.0;   // kroky 1/16 -> doby
    return res;
}

void MainView::onSelTrack (const juce::var& v)
{
    selTrackId = v["track"].toString();
    engine.setSelectedSlot (engine.getOrCreateSlot (v["track"].toString()));
    {
        std::vector<int> ex;
        if (const auto* a = v.getProperty ("arm", {}).getArray()) for (const auto& x : *a) ex.push_back (engine.getOrCreateSlot (x.toString()));
        engine.setExtraSlots (ex);
    }
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
            if (loadFxForTrack (trackId, idx, d, nullptr, v["params"]) && ! (bool) v.getProperty ("quiet", false))
                onOpenFx (v);
            return;
        }
}

std::unique_ptr<juce::AudioPluginInstance> MainView::createInstance (const juce::PluginDescription& d, const juce::MemoryBlock* state, bool instrument)
{
    double sr = 44100; int bs = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice()) { sr = dev->getCurrentSampleRate(); bs = dev->getCurrentBufferSizeSamples(); }
    juce::String err;
    const bool builtin = BuiltinFx::isBuiltin (d);
    if (! builtin) CrashGuard::beginLoad (d.createIdentifierString(), d.name);
    // nase pluginy (Korgus Inspire...) vzdy z priecinka programu - stara kopia v priecinku VST3 by mala prednost
    juce::PluginDescription dd (d);
    if (! builtin && d.pluginFormatName == "VST3" && juce::File::isAbsolutePath (d.fileOrIdentifier))
    {
        const juce::File cur (d.fileOrIdentifier);
        auto fname = cur.getFileName();
        const bool renamed = fname.equalsIgnoreCase ("Korgus Inspire.vst3");   // Korgus Inspire -> Korgus Inspiria (rovnake ID)
        if (renamed) fname = "Korgus Inspiria.vst3";
        const auto mine = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile (fname);
        if (fname.startsWithIgnoreCase ("Korgus") && mine.exists() && mine != cur)
        {
            dd.fileOrIdentifier = mine.getFullPathName();
            if (renamed) dd.name = "Korgus Inspiria";
        }
    }
    auto inst = builtin ? BuiltinFx::create (d) : formatManager.createPluginInstance (dd, sr, bs, err);
    // Korgus Inspiria: zapnut vstup pre hlas (sidechain) - vocoder
    if (inst != nullptr && ! builtin && inst->getName().startsWithIgnoreCase ("Korgus Inspiria") && inst->getBusCount (true) > 1)
    {
        auto lay = inst->getBusesLayout();
        lay.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        if (inst->checkBusesLayoutSupported (lay)) inst->setBusesLayout (lay);
    }
    if (inst == nullptr)
    {
        if (! builtin) CrashGuard::endLoad();
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
    if (! builtin) { CrashGuard::registerPlugin (inst.get(), d.createIdentifierString(), d.name); CrashGuard::endLoad(); }
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

bool MainView::loadFxForTrack (const juce::String& trackId, int idx, const juce::PluginDescription& d, const juce::MemoryBlock* state, const juce::var& params)
{
    auto inst = createInstance (d, state, false);
    if (inst == nullptr) return false;
    // predvolba: hodnoty parametrov podla ID (v jednotkach parametra, pri vybere index)
    if (auto* po = params.getDynamicObject())
        for (auto* pr : inst->getParameters())
            if (auto* hp = dynamic_cast<juce::AudioPluginInstance::HostedParameter*> (pr))
                if (po->hasProperty (hp->getParameterID()))
                    hp->setValue (hp->getValueForText (po->getProperty (hp->getParameterID()).toString()));
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

bool MainView::setArp (const juce::String& trackId, bool on, const juce::MemoryBlock* state)
{
    pluginWindows.erase (trackId + "#arp");
    const int slot = engine.getOrCreateSlot (trackId);
    if (! on)
    {
        engine.setTrackMidiFx (slot, nullptr);
        arpTracks.erase (trackId);
    }
    else
    {
        auto inst = BuiltinFx::create (BuiltinFx::arpDescription());
        if (inst == nullptr) return false;
        if (state != nullptr && state->getSize() > 0) inst->setStateInformation (state->getData(), (int) state->getSize());
        engine.setTrackMidiFx (slot, std::move (inst));
        arpTracks.insert (trackId);
    }
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
    bootLog ("rozhranie nacitane (WebView2 + stranka)");
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
        const bool hasInspiria = exeDir.getChildFile ("Korgus Inspiria.vst3").exists();
        auto bundled = [&exeDir, hasInspiria] (const juce::PluginDescription& d)   // nase pluginy dodane s programom maju vzdy prednost
        {
            if (! juce::File::isAbsolutePath (d.fileOrIdentifier)) return false;
            const auto fn = juce::File (d.fileOrIdentifier).getFileName();
            if (hasInspiria && fn.equalsIgnoreCase ("Korgus Inspire.vst3")) return true;   // stare meno - nahradene Inspiriou
            if (fn.equalsIgnoreCase ("Korgus Master.vst3")) return true;                     // VST3 Master - nahradeny vstavanym
            return fn.startsWithIgnoreCase ("Korgus") && exeDir.getChildFile (fn).exists();
        };
        for (const auto& d : knownPlugins.getTypes())
            if ((! inExeDir (d) || (hasInspiria && juce::File (d.fileOrIdentifier).getFileName().equalsIgnoreCase ("Korgus Inspire.vst3"))) && bundled (d))
                knownPlugins.removeType (d);   // stara kopia v priecinku VST3 / stare meno
        for (const auto& d : knownPlugins.getTypes())
            if (juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier).getFileName().equalsIgnoreCase ("Korgus Master.vst3"))
                knownPlugins.removeType (d);   // VST3 Master sa nepouziva
        for (const auto& d : knownPlugins.getTypes())
            if (inExeDir (d) && (elsewhere (d) || ! juce::File (d.fileOrIdentifier).exists())) knownPlugins.removeType (d);   // aj zmazane/premenovane

        const auto files = exeDir.findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3");
        for (auto* fmt : formatManager.getFormats())
        {
            if (fmt->getName() != "VST3") continue;
            for (const auto& f : files)
            {
                if (hasInspiria && f.getFileName().equalsIgnoreCase ("Korgus Inspire.vst3")) continue;   // stara verzia so starym menom
                if (f.getFileName().equalsIgnoreCase ("Korgus Master.vst3")) continue;   // VST3 Master sa nepouziva (je vstavany efekt)
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
        { auto o = obj(); put (o, "ver", currentVersion()); emit ("appInfo", o); }
        emit ("bootDone", obj());
        juce::MessageManager::callAsync ([] { bootLog ("hotovo"); });
        sendRecent();
        sendCrashInfo();
        checkForUpdate();
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
        bootLog ("(pauza pred krokom)");
        fn();
        bootLog (sp->bootSteps[sp->bootIndex].first);
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
    list.removeIf ([this] (const juce::PluginDescription& d) { return isHeld (d) && ! BuiltinFx::isBuiltin (d) && d.createIdentifierString() != crashSkipUid; });
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
    juce::Array<juce::var> bl;
    for (const auto& b : blockedPlugins)
    {
        auto o = obj(); put (o, "uid", b.upToFirstOccurrenceOf ("\t", false, false)); put (o, "name", b.fromFirstOccurrenceOf ("\t", false, false));
        bl.add (o);
    }
    emit ("blockedPlugins", juce::var (bl));
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
    for (const auto& id : arpTracks) { auto t = entry (id); put (t, "arp", true); }
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
    for (const auto& id : arpTracks)
        if (auto* p = engine.getMidiFx (engine.getOrCreateSlot (id)))
        {
            juce::MemoryBlock mb;
            p->getStateInformation (mb);
            auto t = entry (id);
            auto a = obj(); put (a, "state", mb.toBase64Encoding());
            put (t, "arp", a);
        }
    return o;
}

void MainView::restorePluginStates (const juce::var& vst)
{
    juce::StringArray missing;
    auto load = [this, &missing] (const juce::var& t, juce::PluginDescription& desc, juce::MemoryBlock& mb)
    {
        const auto xml = juce::parseXML (t["desc"].toString());
        if (xml == nullptr || ! desc.loadFromXml (*xml)) return false;
        if (juce::File::isAbsolutePath (desc.fileOrIdentifier) && ! juce::File (desc.fileOrIdentifier).exists())
        {
            // plugin sa presunul (napr. vedla programu alebo iny pocitac)
            bool found = false;
            for (const auto& k : knownPlugins.getTypes())
                if (k.uniqueId == desc.uniqueId && k.name == desc.name && k.pluginFormatName == desc.pluginFormatName) { desc = k; found = true; break; }
            if (! found)
                for (const auto& k : knownPlugins.getTypes())
                    if (k.name == desc.name && k.pluginFormatName == desc.pluginFormatName) { desc = k; found = true; break; }
            if (! found)   // premenovany plugin (Korgus Nova -> Korgus Inspire -> Korgus Inspiria): rovnake ID
                for (const auto& k : knownPlugins.getTypes())
                    if (k.uniqueId == desc.uniqueId && k.uniqueId != 0 && k.pluginFormatName == desc.pluginFormatName && k.manufacturerName == desc.manufacturerName) { desc = k; found = true; break; }
            if (! found)
            {
                missing.addIfNotAlreadyThere (desc.name + (desc.manufacturerName.isNotEmpty() ? " (" + desc.manufacturerName + ")" : juce::String()));
                return false;
            }
        }
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
            {
                if (isHeld (desc)) holdEntry (trackId, prop.value, -1);
                else loadPluginForTrack (trackId, desc, &mb);
            }
            if (prop.value["arp"].isObject())
            {
                juce::MemoryBlock am;
                am.fromBase64Encoding (prop.value["arp"]["state"].toString());
                setArp (trackId, true, &am);
            }
            if (const auto* fx = prop.value["fx"].getArray())
                for (const auto& f : *fx)
                {
                    juce::PluginDescription fd;
                    juce::MemoryBlock fm;
                    if (load (f, fd, fm))
                    {
                        if (isHeld (fd)) holdEntry (trackId, f, (int) f.getProperty ("idx", 0));
                        else loadFxForTrack (trackId, (int) f.getProperty ("idx", 0), fd, &fm);
                    }
                }
        }
    }
    if (! missing.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, sk ("Chýbajúce pluginy"),
                                                sk ("Tieto pluginy sa na tomto počítači nenašli – nainštaluj ich a nechaj program vyhľadať pluginy:") + "\n\n- "
                                                + missing.joinIntoString ("\n- "));
}

void MainView::saveSession()
{
    auto st = collectPluginStates();
    mergeHeld (st);
    dataDir.getChildFile ("session.json").replaceWithText (juce::JSON::toString (st));
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
            { auto st = collectPluginStates(); mergeHeld (st); d->setProperty ("vst", st); }
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
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [data, sp, kind] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f != juce::File()) f.replaceWithData (data.getData(), data.getSize());
                              if (f != juce::File() && kind == "s92" && sp != nullptr) sp->addRecent (f);
                          });
}

namespace
{
    // zbaleny projekt (.kpack = zip): project.json + audio/ (WAV prevedene do FLAC) + info.txt
    struct PackJob : juce::ThreadWithProgressWindow
    {
        PackJob (juce::File target, juce::String json, juce::String info, std::vector<std::pair<juce::String, juce::File>> files)
            : ThreadWithProgressWindow (sk ("Balím projekt"), true, true),
              out (std::move (target)), projectJson (std::move (json)), infoTxt (std::move (info)), audio (std::move (files)) {}

        void run() override
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            juce::FlacAudioFormat flac;
            juce::ZipFile::Builder zb;
            int k = 0;
            for (const auto& [id, f] : audio)
            {
                if (threadShouldExit()) return;
                setStatusMessage (sk ("Balím zvuk ") + juce::String (k + 1) + " / " + juce::String ((int) audio.size()) + "…");
                setProgress (0.8 * k / juce::jmax (1, (int) audio.size()));
                ++k;
                juce::MemoryBlock mb;
                juce::String name = f.getFileName();
                bool done = false;
                if (f.hasFileExtension ("wav"))
                    if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f)); r != nullptr && r->numChannels <= 8
                        && r->sampleRate >= 8000 && r->sampleRate <= 192000)
                    {
                        auto* ms = new juce::MemoryOutputStream (mb, false);
                        std::unique_ptr<juce::OutputStream> os (ms);
                        const int bits = juce::jlimit (16, 24, (int) r->bitsPerSample);
                        if (auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (r->sampleRate)
                                                                    .withNumChannels ((int) r->numChannels).withBitsPerSample (bits)))
                        {
                            done = w->writeFromAudioReader (*r, 0, r->lengthInSamples);
                            w.reset();
                            name = id + ".flac";
                        }
                    }
                if (! done) { mb.reset(); if (! f.loadFileAsData (mb)) continue; name = id + f.getFileExtension(); }
                zb.addEntry (new juce::MemoryInputStream (mb, true), 0, "audio/" + name, juce::Time::getCurrentTime());
            }
            zb.addEntry (new juce::MemoryInputStream (projectJson.toRawUTF8(), projectJson.getNumBytesAsUTF8(), true), 6, "project.json", juce::Time::getCurrentTime());
            zb.addEntry (new juce::MemoryInputStream (infoTxt.toRawUTF8(), infoTxt.getNumBytesAsUTF8(), true), 6, "info.txt", juce::Time::getCurrentTime());
            setStatusMessage (sk ("Zapisujem súbor…"));
            const auto tmp = out.getSiblingFile (out.getFileName() + ".tmp");
            {
                juce::FileOutputStream fos (tmp);
                if (! fos.openedOk()) { error = sk ("Súbor sa nedá zapísať."); return; }
                fos.setPosition (0); fos.truncate();
                double prog = 0;
                if (! zb.writeToStream (fos, &prog)) { error = sk ("Zápis sa nepodaril."); return; }
            }
            ok = tmp.moveFileTo (out);
            if (! ok) error = sk ("Súbor sa nedá zapísať.");
        }
        void threadComplete (bool cancelled) override
        {
            if (! cancelled)
            {
                if (ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, sk ("Projekt je zbalený"),
                                                               out.getFullPathName() + "\n" + juce::String (out.getSize() / 1048576.0, 1) + " MB\n\n"
                                                               + sk ("Na inom počítači ho otvor cez Súbor → Otvoriť."));
                else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, sk ("Zbaliť projekt"), error);
            }
            out.getSiblingFile (out.getFileName() + ".tmp").deleteFile();
            delete this;
        }

        juce::File out;
        juce::String projectJson, infoTxt, error;
        std::vector<std::pair<juce::String, juce::File>> audio;
        bool ok = false;
    };
}

void MainView::onPackProject (const juce::var& v)
{
    auto j = juce::JSON::parse (v["text"].toString());
    auto* d = j.getDynamicObject();
    if (d == nullptr) return;
    const auto vst = collectPluginStates();
    d->setProperty ("vst", vst);
    // vsetky pouzite zvuky (klipy, vzorky bicich, referencia)
    std::vector<std::pair<juce::String, juce::File>> files;
    juce::StringArray seen;
    auto addId = [&] (const juce::String& id)
    {
        if (id.isEmpty() || seen.contains (id)) return;
        seen.add (id);
        const auto f = store->fileFor (id);
        if (f.existsAsFile()) files.push_back ({ id, f });
    };
    if (const auto* tracks = j["tracks"].getArray())
        for (const auto& t : *tracks)
        {
            if (const auto* parts = t["parts"].getArray())
                for (const auto& p : *parts) addId (p["aid"].toString());
            if (auto* sm = t["smp"].getDynamicObject())
                for (const auto& prop : sm->getProperties()) addId (prop.value.toString());
        }
    addId (j["ref"]["id"].toString());
    // zoznam pluginov do info.txt
    juce::StringArray plugs;
    auto addPlug = [&] (const juce::var& t)
    {
        juce::PluginDescription pd;
        if (auto x = juce::parseXML (t["desc"].toString()); x != nullptr && pd.loadFromXml (*x) && pd.pluginFormatName != BuiltinFx::formatName)
            plugs.addIfNotAlreadyThere (pd.name + (pd.manufacturerName.isNotEmpty() ? " (" + pd.manufacturerName + ")" : juce::String()) + " – " + pd.pluginFormatName);
    };
    if (auto* vo = vst.getDynamicObject())
        for (const auto& prop : vo->getProperties())
        {
            if (prop.value.hasProperty ("desc")) addPlug (prop.value);
            if (const auto* fx = prop.value["fx"].getArray()) for (const auto& f : *fx) addPlug (f);
        }
    juce::String info = sk ("Korgus Sequencer – zbalený projekt\r\n\r\nOtvor ho v programe: Súbor → Otvoriť (súbor .kpack).\r\n\r\n");
    info << sk ("Zvukové súbory: ") << (int) files.size() << "\r\n\r\n";
    info << (plugs.isEmpty() ? sk ("Projekt nepotrebuje žiadne ďalšie VST pluginy.\r\n")
                             : sk ("Potrebné VST pluginy (musia byť nainštalované aj na druhom počítači):\r\n- ") + plugs.joinIntoString ("\r\n- ") + "\r\n");
    const auto json = juce::JSON::toString (j, true);
    auto name = juce::File::createLegalFileName (v["name"].toString().trim());
    if (name.isEmpty()) name = "projekt";
    chooser = std::make_unique<juce::FileChooser> (sk ("Zbaliť projekt"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".kpack"), "*.kpack");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [json, info, files] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("kpack")) f = f.withFileExtension ("kpack");
                              (new PackJob (f, json, info, files))->launchThread();
                          });
}

void MainView::onOpenSong (const juce::var& v)
{
    juce::Component::SafePointer<MainView> sp (this);
    auto run = [sp] (juce::File f)
                          {
                              if (sp == nullptr || ! f.existsAsFile()) return;
                              sp->addRecent (f);
                              juce::var j;
                              if (f.hasFileExtension ("kpack"))
                              {
                                  // zbaleny projekt: zvuky do skladu, potom ako bezna piesen
                                  juce::ZipFile zf (f);
                                  for (int i = 0; i < zf.getNumEntries(); ++i)
                                  {
                                      const auto* e = zf.getEntry (i);
                                      std::unique_ptr<juce::InputStream> in (zf.createStreamForEntry (i));
                                      if (e == nullptr || in == nullptr) continue;
                                      if (e->filename == "project.json") j = juce::JSON::parse (in->readEntireStreamAsString());
                                      else if (e->filename.startsWith ("audio/"))
                                      {
                                          const auto fn = e->filename.fromFirstOccurrenceOf ("audio/", false, false);
                                          juce::MemoryBlock mb;
                                          in->readIntoMemoryBlock (mb);
                                          sp->store->addFromData (fn.upToLastOccurrenceOf (".", false, false), fn, mb);
                                      }
                                  }
                                  if (! j.isObject())
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, sk ("Otvoriť"), sk ("Súbor nie je platný zbalený projekt."));
                                      return;
                                  }
                              }
                              else j = juce::JSON::parse (f.loadFileAsString());
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
                              sp->held = obj();
                              juce::MessageManager::callAsync ([sp, vst] { if (sp != nullptr && vst.isObject()) sp->restorePluginStates (vst); });
                          };
    if (const auto path = v["path"].toString(); juce::File::isAbsolutePath (path)) { run (juce::File (path)); return; }
    chooser = std::make_unique<juce::FileChooser> (sk ("Otvoriť pieseň"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.s92;*.kpack;*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [run] (const juce::FileChooser& fc) { run (fc.getResult()); });
}

//==============================================================================
// automaticke zalohy: kazdych 5 minut (ak sa piesen zmenila), najviac 60 suborov
juce::File MainView::backupDir() const { return dataDir.getChildFile ("zalohy"); }

void MainView::backupMaybe (const juce::String& text)
{
    const auto now = juce::Time::getCurrentTime();
    if (text == lastBackupText) return;
    if (lastBackupTime.toMilliseconds() != 0 && (now - lastBackupTime).inMinutes() < 5.0) return;
    auto dir = backupDir();
    dir.createDirectory();
    auto j = juce::JSON::parse (text);
    if (! j.isObject()) return;
    if (auto* d = j.getDynamicObject()) d->setProperty ("vst", collectPluginStates());
    const auto f = dir.getChildFile (now.formatted ("%Y-%m-%d_%H-%M-%S") + ".json");
    if (! f.replaceWithText (juce::JSON::toString (j, true))) return;
    lastBackupText = text;
    lastBackupTime = now;
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (int i = 60; i < files.size(); ++i) files.getReference (i).deleteFile();
}

void MainView::sendBackups()
{
    auto files = backupDir().findChildFiles (juce::File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    juce::Array<juce::var> arr;
    for (const auto& f : files)
    {
        auto o = obj();
        put (o, "name", f.getFileName());
        put (o, "time", f.getFileNameWithoutExtension());
        int tracks = 0; double tempo = 0;
        {
            const auto j = juce::JSON::parse (f.loadFileAsString());
            if (const auto* t = j["tracks"].getArray()) tracks = t->size();
            tempo = (double) j.getProperty ("tempo", 0.0);
        }
        put (o, "tracks", tracks); put (o, "tempo", tempo);
        arr.add (o);
    }
    auto o = obj(); put (o, "list", arr);
    emit ("backups", o);
}

void MainView::loadBackup (const juce::String& name)
{
    const auto f = backupDir().getChildFile (juce::File::createLegalFileName (name));
    if (! f.existsAsFile()) return;
    auto j = juce::JSON::parse (f.loadFileAsString());
    if (! j.isObject()) return;
    const auto vst = j["vst"];
    if (auto* d = j.getDynamicObject()) d->removeProperty ("vst");
    sessionApplied = true;
    auto o = obj();
    put (o, "text", juce::JSON::toString (j, true));
    emit ("songLoaded", o);
    held = obj();
    juce::Component::SafePointer<MainView> sp (this);
    juce::MessageManager::callAsync ([sp, vst] { if (sp != nullptr && vst.isObject()) sp->restorePluginStates (vst); });
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
    },
    [sp] (const juce::PluginDescription& d) { return sp != nullptr && sp->isOwnPlugin (d); },
    [sp] (const juce::PluginDescription& d) { return sp != nullptr && sp->enabledPlugins.contains (d.createIdentifierString()); },
    [sp] (const juce::StringArray& uids, bool on)
    {
        if (sp == nullptr) return;
        for (const auto& u : uids) { if (on) sp->enabledPlugins.addIfNotAlreadyThere (u); else sp->enabledPlugins.removeString (u); }
        sp->props.getUserSettings()->setValue ("enabledPlugins", sp->enabledPlugins.joinIntoString ("\n"));
        sp->props.saveIfNeeded();
        if (on) { auto h = sp->held; sp->held = obj(); sp->restorePluginStates (h); }   // vrati odlozene pluginy piesne
        sp->sendPluginList();
    },
    [sp] { return sp != nullptr ? sp->blockedPlugins : juce::StringArray(); },
    [sp] (const juce::String& uid) { if (sp != nullptr) sp->unblockPlugin (uid); });
}

void MainView::sendMeters()
{
    juce::Array<juce::var> ids, vals;
    bool any = false;
    auto addId = [&] (const juce::String& id)
    {
        const auto m = engine.takeMeter (engine.getOrCreateSlot (id));
        ids.add (id);
        vals.add (std::round (m.first * 1000.0f) / 1000.0f); vals.add (std::round (m.second * 1000.0f) / 1000.0f);
        any = any || m.first > 0.0005f || m.second > 0.0005f;
    };
    if (const auto* tracks = lastSong["tracks"].getArray())
        for (const auto& t : *tracks) if (t["id"].toString().isNotEmpty()) addId (t["id"].toString());
    addId ("__aux1"); addId ("__aux2"); addId ("__master");
    if (! any && ! metersLive) return;
    metersLive = any;
    auto o = obj(); put (o, "ids", ids); put (o, "v", vals);
    emit ("meters", o);
}

void MainView::sendMidiCc()
{
    Engine::CcIn buf[512];
    const int n = engine.takeCcIn (buf, 512);
    if (n == 0) return;
    // pohyb jedneho ovladaca zlucime (posledna hodnota), stlacenie a pustenie tlacidla zostane
    juce::Array<juce::var> arr;
    int lastKey = -1, lastVal = 0;
    for (int k = 0; k < n; ++k)
    {
        const int key = (buf[k].ch - 1) * 128 + buf[k].cc, val = buf[k].val;
        if (key == lastKey && (val >= 64) == (lastVal >= 64) && ! arr.isEmpty()) arr.removeLast();
        arr.add (juce::Array<juce::var> { key / 128 + 1, key % 128, val });
        lastKey = key; lastVal = val;
    }
    auto o = obj(); put (o, "list", arr);
    emit ("midiCC", o);
}

// ladicka: vyska tonu zo vstupu (YIN), poslana do rozhrania
void MainView::sendTuner()
{
    const double sr = engine.getSampleRate();
    const unsigned w = engine.tunerW.load (std::memory_order_acquire);
    constexpr int N = 4096;
    std::vector<float> x ((size_t) N);
    double rms = 0;
    for (int i = 0; i < N; ++i) { const float v = engine.tunerBuf[(w - (unsigned) N + (unsigned) i) & (Engine::tunerSize - 1)]; x[(size_t) i] = v; rms += v * v; }
    rms = std::sqrt (rms / N);
    auto o = obj();
    put (o, "level", juce::Decibels::gainToDecibels ((float) rms, -100.0f));
    double hz = 0;
    if (rms > 0.003)
    {
        const int W = 2048, tauMax = juce::jmin (N - W - 2, (int) (sr / 50.0)), tauMin = juce::jmax (2, (int) (sr / 1500.0));
        std::vector<double> d ((size_t) tauMax + 2, 0.0);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0;
            for (int i = 0; i < W; ++i) { const double df = x[(size_t) i] - x[(size_t) (i + tau)]; s += df * df; }
            d[(size_t) tau] = s;
        }
        // kumulativny priemer (CMND) a prvy hlboky minimum pod prahom
        double run = 0; int best = -1;
        std::vector<double> c ((size_t) tauMax + 2, 1.0);
        for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; c[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0; }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (c[(size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && c[(size_t) tau + 1] < c[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best > 0)
        {
            const double a = c[(size_t) best - 1], b = c[(size_t) best], cc = c[(size_t) best + 1];
            const double den = a - 2 * b + cc;
            const double t = best + (std::abs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0);
            hz = sr / t;
        }
    }
    put (o, "hz", hz);
    emit ("tuner", o);
}

void MainView::timerCallback()
{
    if (recOn || recFinishing)
        for (size_t i = 0; i < recTracks.size(); ++i) engine.drainRecording ((int) i, recTracks[i].l, recTracks[i].r);
    if (recOn) sendLivePeaks();
    if (recFinishing && juce::Time::getMillisecondCounter() - recStopTime > 250) finishRecording();
    if (exporting.load()) return;

    sendMeters();
    sendMidiCc();
    if (engine.tunerCh.load() >= 0 && (++tunerTick & 1) == 0) sendTuner();

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

// prechodove efekty (riser, reverse cinel, whoosh, impact, downlifter) - syntetizovane v tempe piesne
static juce::File makeTransitionFile (const juce::String& kind, double bpm)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Korgus Efekty");
    dir.createDirectory();
    const int bp = juce::jlimit (40, 240, juce::roundToInt (bpm));
    const auto f = dir.getChildFile (kind + "_" + juce::String (bp) + "bpm.wav");
    if (f.existsAsFile()) return f;
    constexpr double sr = 44100.0;
    const double beat = 60.0 / bp;
    double secs = kind == "riser" || kind == "down" ? 8 * beat : kind == "impact" ? 3.0 : 4 * beat;
    const int n = (int) (secs * sr);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rnd (1234 + kind.hashCode());
    struct Svf { float ic1 = 0, ic2 = 0; float bp (float x, float hz, float q) { const float g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, 18000.0f, hz) / (float) sr), k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2, v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; hp = x - k * v1 - v2; return v1; } float hp = 0; };
    Svf fl, fr;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) n;
        const float nl = rnd.nextFloat() * 2 - 1, nr = rnd.nextFloat() * 2 - 1;
        float l = 0, r = 0;
        if (kind == "riser" || kind == "down")
        {
            const float u = kind == "riser" ? t : 1.0f - t;                // 0 -> 1 nahor
            const float hz = 250.0f * std::pow (40.0f, u), amp = std::pow (u, 1.6f) * (kind == "down" ? std::pow (1.0f - t, 0.3f) : 1.0f);
            l = fl.bp (nl, hz, 2.5f) * 1.6f; r = fr.bp (nr, hz * 1.03f, 2.5f) * 1.6f;
            ph += (180.0 + 1400.0 * u * u) / sr; const float tone = 0.18f * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
            l = (l + tone) * amp; r = (r + tone) * amp;
        }
        else if (kind == "revcym")
        {
            const float amp = std::pow (t, 3.0f);
            fl.bp (nl, 9000, 0.7f); fr.bp (nr, 9000, 0.7f);
            l = fl.hp * amp; r = fr.hp * amp;
            if (i > n - 200) { const float e = (float) (n - i) / 200.0f; l *= e; r *= e; }
        }
        else if (kind == "whoosh")
        {
            const float env = std::sin (juce::MathConstants<float>::pi * std::pow (t, 0.8f)), hz = 400.0f * std::pow (20.0f, std::sin (juce::MathConstants<float>::pi * t));
            l = fl.bp (nl, hz, 1.5f) * env * 1.4f; r = fr.bp (nr, hz * 1.05f, 1.5f) * env * 1.4f;
            const float pan = t;   // prelet zlava doprava
            l *= 1.2f - pan; r *= 0.2f + pan;
        }
        else   // impact: hlboky uder + sum + dlhy chvost
        {
            const float ts = (float) i / (float) sr;
            ph += (34.0 + 50.0 * std::exp (-ts * 9.0)) / sr;
            const float boom = (float) std::sin (juce::MathConstants<double>::twoPi * ph) * std::exp (-ts * 1.6f);
            const float click = (nl * 0.8f) * std::exp (-ts * 40.0f);
            const float tail = fl.bp (nl, 700.0f * std::exp (-ts * 0.6f) + 120.0f, 0.6f) * 0.5f * std::exp (-ts * 1.3f);
            const float tailR = fr.bp (nr, 700.0f * std::exp (-ts * 0.6f) + 120.0f, 0.6f) * 0.5f * std::exp (-ts * 1.3f);
            l = boom + click + tail; r = boom + click * 0.9f + tailR;
        }
        b.setSample (0, i, l); b.setSample (1, i, r);
    }
    const float pk = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n));
    if (pk > 1.0e-6f) b.applyGain (0.85f / pk);
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return {};
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (b, 0, n);
    w.reset();
    return f;
}

void MainView::onImportAudio (const juce::var& v)
{
    if (v.hasProperty ("transition"))
    {
        const auto f = makeTransitionFile (v["transition"].toString(), (double) v.getProperty ("bpm", 120.0));
        if (f.existsAsFile()) finishImport (f, v["track"].toString(), (double) v.getProperty ("bar", 0), false);
        return;
    }
    const auto track = v["track"].toString();
    const double bar = (double) v.getProperty ("bar", 0);
    if (const auto path = v["path"].toString(); juce::File::isAbsolutePath (path))
    {
        // z prehliadaca zvukov v programe
        if (juce::File (path).existsAsFile()) finishImport (juce::File (path), track, bar, false);
        return;
    }
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
    const double start = juce::jlimit (0.0, juce::jmax (0.0, end - 1.0), (double) v.getProperty ("start", 0.0));
    chooser = std::make_unique<juce::FileChooser> (sk ("Export WAV"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("piesen.wav"), "*.wav");
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp, end, start] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (sp == nullptr || f == juce::File()) return;
                              f = f.withFileExtension ("wav");
                              sp->engine.exportStart = start;
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
    // volitelne: vlastna pieseň na export (napr. "Inspiracia" – len jedna stopa/cast), inak aktualna
    const juce::var songOverride = v["song"].toString().isNotEmpty() ? juce::JSON::parse (v["song"].toString()) : juce::var();
    const auto defName = v["name"].toString().isNotEmpty() ? juce::File::createLegalFileName (v["name"].toString()) : juce::String ("piesen");
    const double start = juce::jmax (0.0, (double) v.getProperty ("start", 0.0));
    chooser = std::make_unique<juce::FileChooser> (sk ("Export MP3"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (defName + ".mp3"), "*.mp3");
    juce::Component::SafePointer<MainView> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp, end, lufs, kbps, songOverride, start] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (sp == nullptr || f == juce::File()) return;
                              f = f.withFileExtension ("mp3");
                              sp->engine.exportStart = start;
                              auto sd = sp->parseCurrent (songOverride.isVoid() ? sp->lastSong : songOverride);
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

namespace
{
    struct VideoJob : juce::ThreadWithProgressWindow
    {
        VideoJob (Engine& e, std::unique_ptr<SongData> s, double endStep, VideoOptions o, juce::File f, std::function<void (bool, juce::String)> onDone)
            : ThreadWithProgressWindow (sk ("Export videa – ") + f.getFileName(), true, true),
              engine (e), sd (std::move (s)), end (endStep), opt (std::move (o)), file (f), done (std::move (onDone)) {}

        void run() override
        {
            const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_video.wav");
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
                if (! engine.renderOffline (*sd, end, 3.0, *w, [this] (double p) { setProgress (p * 0.3); return ! threadShouldExit(); })) { tmp.deleteFile(); return; }
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
            int n = mix.getNumSamples();
            while (n > (int) sr && mix.getMagnitude (0, n - 64, 64) < 1.0e-4f && mix.getMagnitude (1, n - 64, 64) < 1.0e-4f) n -= 64;
            mix.setSize (2, juce::jmin (mix.getNumSamples(), n + (int) (sr * 0.5)), true);
            setStatusMessage (sk ("Dorovnávam hlasitosť…"));
            normaliseLoudness (mix, sr, -14.0, -1.0);
            setStatusMessage (sk ("Vytváram video (môže to chvíľu trvať)…"));
            ok = writeMp4 (mix, sr, opt, file, error, [this] (double p) { setProgress ((opt.alsoMp3 ? 0.27 : 0.32) + p * (opt.alsoMp3 ? 0.6 : 0.68)); return ! threadShouldExit(); });
            if (ok && opt.alsoMp3 && ! threadShouldExit())
            {
                setStatusMessage (sk ("Ukladám MP3…"));
                juce::String e2;
                if (! writeMp3 (mix, sr, 320, file.withFileExtension ("mp3"), e2, [this] (double p) { setProgress (0.87 + p * 0.13); return ! threadShouldExit(); }))
                    error = e2;
            }
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (ok && ! cancelled, ok ? juce::String() : error);
            delete this;
        }

        Engine& engine;
        std::unique_ptr<SongData> sd;
        double end;
        VideoOptions opt;
        juce::File file;
        std::function<void (bool, juce::String)> done;
        juce::String error;
        bool ok = false;
    };
}

void MainView::onExportVideo (const juce::var& v)
{
    if (exporting.load() || lastSong.isVoid()) return;
    VideoOptions o;
    o.title = v["title"].toString();
    o.subtitle = v["subtitle"].toString();
    o.style = (int) v.getProperty ("style", 0);
    o.portrait = (bool) v.getProperty ("portrait", true);
    o.alsoMp3 = (bool) v.getProperty ("mp3", false);
    if (const auto* ly = v["lyrics"].getArray())
        for (const auto& l : *ly)
            if (l["x"].toString().isNotEmpty()) { o.lyrT.add ((double) l["t"]); o.lyrX.add (l["x"].toString()); }
    const double end = juce::jmax (16.0, (double) v.getProperty ("end", 64));
    const bool useImage = (bool) v.getProperty ("useImage", false);
    juce::Component::SafePointer<MainView> sp (this);

    auto chooseTarget = [sp, end] (VideoOptions opt)
    {
        if (sp == nullptr) return;
        const auto name = juce::File::createLegalFileName (opt.title.isNotEmpty() ? opt.title : sk ("piesen")) + ".mp4";
        sp->chooser = std::make_unique<juce::FileChooser> (sk ("Export videa (MP4)"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), "*.mp4");
        sp->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [sp, end, opt] (const juce::FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (sp == nullptr || f == juce::File()) return;
                                      f = f.withFileExtension ("mp4");
                                      auto sd = sp->parseCurrent (sp->lastSong);
                                      if (sd == nullptr) return;
                                      sp->engine.stop();
                                      sp->exporting = true;
                                      auto* job = new VideoJob (sp->engine, std::move (sd), end, opt, f, [sp, f] (bool good, juce::String info)
                                      {
                                          if (sp == nullptr) return;
                                          sp->exporting = false;
                                          sp->lastSentPos = -1;
                                          auto o2 = obj();
                                          put (o2, "ok", good); put (o2, "name", f.getFullPathName()); put (o2, "info", info);
                                          sp->emit ("exported", o2);
                                      });
                                      job->launchThread();
                                  });
    };

    if (! useImage) { chooseTarget (o); return; }
    chooser = std::make_unique<juce::FileChooser> (sk ("Obrázok na pozadie videa"), juce::File::getSpecialLocation (juce::File::userPicturesDirectory), "*.jpg;*.jpeg;*.png");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp, o, chooseTarget] (const juce::FileChooser& fc)
                          {
                              if (sp == nullptr) return;
                              auto opt = o;
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) opt.background = juce::ImageFileFormat::loadFrom (f);
                              juce::MessageManager::callAsync ([chooseTarget, opt] { chooseTarget (opt); });
                          });
}

namespace
{
    // stav vstavaneho efektu s nastavenymi parametrami (podla ID)
    juce::MemoryBlock fxState (const juce::String& key, const std::map<juce::String, float>& vals, juce::PluginDescription& descOut)
    {
        for (const auto& d : BuiltinFx::descriptions())
            if (d.fileOrIdentifier == "korgus:" + key)
            {
                descOut = d;
                auto inst = BuiltinFx::create (d);
                for (auto* p : inst->getParameters())
                    if (auto* hp = dynamic_cast<juce::AudioPluginInstance::HostedParameter*> (p))
                        if (auto it = vals.find (hp->getParameterID()); it != vals.end())
                            p->setValue (p->getValueForText (juce::String (it->second, 3)));
                juce::MemoryBlock mb;
                inst->getStateInformation (mb);
                return mb;
            }
        return {};
    }

    double bandRelDb (const std::vector<float>& mono, double sr, bool low, double fc)
    {
        std::vector<float> x (mono);
        for (int k = 0; k < 2; ++k)
        {
            juce::IIRFilter f;
            f.setCoefficients (low ? juce::IIRCoefficients::makeLowPass (sr, fc) : juce::IIRCoefficients::makeHighPass (sr, fc));
            f.processSamples (x.data(), (int) x.size());
        }
        double e = 0, t = 0;
        for (size_t i = 0; i < x.size(); ++i) { e += (double) x[i] * x[i]; t += (double) mono[i] * mono[i]; }
        return 10.0 * std::log10 ((e + 1.0e-12) / (t + 1.0e-12));
    }

    struct MasteringJob : juce::ThreadWithProgressWindow
    {
        MasteringJob (Engine& e, std::unique_ptr<SongData> s, double endStep, double lufs, int ch, std::function<void (MasteringJob&)> onDone)
            : ThreadWithProgressWindow (sk ("Mastering asistent"), true, true),
              engine (e), sd (std::move (s)), end (endStep), target (lufs), character (ch), done (std::move (onDone)) {}

        void run() override
        {
            const double sr = engine.getSampleRate();
            const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_master.wav");
            tmp.deleteFile();
            {
                std::unique_ptr<juce::OutputStream> os (tmp.createOutputStream().release());
                if (os == nullptr) { error = sk ("Nedá sa zapísať dočasný súbor."); return; }
                juce::WavAudioFormat wav;
                auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (32)
                                                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                if (w == nullptr) { error = sk ("Nedá sa zapísať dočasný súbor."); return; }
                setStatusMessage (sk ("Počúvam mix…"));
                engine.bypassMaster = true;
                const bool r = engine.renderOffline (*sd, end, 1.0, *w, [this] (double p) { setProgress (p * 0.6); return ! threadShouldExit(); });
                engine.bypassMaster = false;
                if (! r) { tmp.deleteFile(); return; }
            }
            juce::AudioBuffer<float> mix;
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (new juce::FileInputStream (tmp), true));
                if (rd == nullptr) { error = sk ("Dočasný súbor sa nedá prečítať."); return; }
                mix.setSize (2, (int) rd->lengthInSamples);
                rd->read (&mix, 0, (int) rd->lengthInSamples, 0, true, true);
            }
            tmp.deleteFile();
            const int n = mix.getNumSamples();
            if (n < (int) sr || mix.getMagnitude (0, n) < 1.0e-4f) { error = sk ("Pieseň je tichá – nie je čo masterovať."); return; }

            setStatusMessage (sk ("Analyzujem…"));
            lufsBefore = measureLufs (mix, sr);
            std::vector<float> mono ((size_t) n);
            for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (mix.getSample (0, i) + mix.getSample (1, i));
            const double lowRel = bandRelDb (mono, sr, true, 150.0), highRel = bandRelDb (mono, sr, false, 4000.0);
            // tonalne ciele podla charakteru
            const double tLow = character == 1 ? -4.0 : (character == 2 ? -5.5 : -5.0);
            const double tHigh = character == 1 ? -18.0 : (character == 2 ? -14.0 : -16.0);
            eqLow = (float) juce::jlimit (-4.0, 4.0, (tLow - lowRel) * 0.6);
            eqHigh = (float) juce::jlimit (-4.0, 5.0, (tHigh - highRel) * 0.6);
            // prah kompresora podla hlasnych miest (90. percentil RMS po 400 ms)
            std::vector<float> lv;
            const int win = (int) (sr * 0.4);
            for (int i = 0; i + win <= n; i += win)
            {
                double e = 0; for (int k = 0; k < win; ++k) e += (double) mono[(size_t) (i + k)] * mono[(size_t) (i + k)];
                const double db = 10.0 * std::log10 (e / win + 1.0e-12);
                if (db > -60.0) lv.push_back ((float) db);
            }
            std::sort (lv.begin(), lv.end());
            const float p90 = lv.empty() ? -20.0f : lv[(size_t) ((lv.size() - 1) * 9 / 10)];
            compThr = juce::jlimit (-40.0f, -6.0f, p90 - 3.0f);

            eqVals = { { "lowcut", 30.0f }, { "lowgain", eqLow }, { "lowfreq", 110.0f }, { "midgain", 0.0f }, { "midfreq", 1000.0f }, { "midq", 1.0f },
                       { "highgain", eqHigh }, { "highfreq", 8000.0f }, { "output", 0.0f } };
            compVals = { { "threshold", compThr }, { "ratio", 2.0f }, { "attack", 25.0f }, { "release", 160.0f }, { "makeup", 0.0f }, { "mix", 100.0f }, { "key", 0.0f } };
            limGain = (float) juce::jlimit (0.0, 18.0, target - lufsBefore);

            // dve skusky retazca - zisk limitera tak, aby sedela cielova hlasitost
            for (int pass = 0; pass < 3 && ! threadShouldExit(); ++pass)
            {
                setStatusMessage (sk ("Dolaďujem hlasitosť…"));
                limVals = { { "gain", limGain }, { "ceiling", -1.0f }, { "release", 60.0f } };
                juce::AudioBuffer<float> b (mix);
                juce::PluginDescription d;
                std::unique_ptr<juce::AudioPluginInstance> chain[3];
                const juce::String keys[3] { "eq", "comp", "limiter" };
                const std::map<juce::String, float>* vals[3] { &eqVals, &compVals, &limVals };
                for (int k = 0; k < 3; ++k)
                {
                    auto st = fxState (keys[k], *vals[k], d);
                    chain[k] = BuiltinFx::create (d);
                    chain[k]->setStateInformation (st.getData(), (int) st.getSize());
                    chain[k]->prepareToPlay (sr, 1024);
                }
                juce::MidiBuffer mb;
                for (int i = 0; i < n; i += 1024)
                {
                    const int len = juce::jmin (1024, n - i);
                    juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, i, len);
                    for (auto& c : chain) c->processBlock (view, mb);
                    if ((i & 0xffff) == 0) setProgress (0.6 + 0.13 * pass + 0.13 * (double) i / n);
                }
                lufsAfter = measureLufs (b, sr);
                if (pass < 2) limGain = (float) juce::jlimit (0.0, 24.0, (double) limGain + (target - lufsAfter));
            }
            ok = ! threadShouldExit();
        }
        void threadComplete (bool cancelled) override
        {
            if (cancelled) ok = false;
            if (done) done (*this);
            delete this;
        }

        Engine& engine;
        std::unique_ptr<SongData> sd;
        double end, target;
        int character;
        std::function<void (MasteringJob&)> done;
        juce::String error;
        bool ok = false;
        double lufsBefore = 0, lufsAfter = 0;
        float eqLow = 0, eqHigh = 0, compThr = -18, limGain = 0;
        std::map<juce::String, float> eqVals, compVals, limVals;
    };
}

void MainView::onMastering (const juce::var& v)
{
    if (exporting.load() || lastSong.isVoid()) return;
    auto sd = parseCurrent (lastSong);
    if (sd == nullptr) return;
    const double end = juce::jmax (16.0, (double) v.getProperty ("end", 64));
    const double target = juce::jlimit (-20.0, -6.0, (double) v.getProperty ("lufs", -14.0));
    const int character = juce::jlimit (0, 2, (int) v.getProperty ("character", 0));
    engine.stop();
    exporting = true;
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new MasteringJob (engine, std::move (sd), end, target, character, [sp] (MasteringJob& j)
    {
        if (sp == nullptr) return;
        sp->exporting = false;
        sp->lastSentPos = -1;
        auto o = obj();
        put (o, "ok", j.ok);
        if (! j.ok) { put (o, "error", j.error.isNotEmpty() ? j.error : juce::String ("cancel")); sp->emit ("mastered", o); return; }
        const juce::String keys[3] { "eq", "comp", "limiter" };
        const std::map<juce::String, float>* vals[3] { &j.eqVals, &j.compVals, &j.limVals };
        for (int k = 0; k < 3; ++k)
        {
            juce::PluginDescription d;
            auto st = fxState (keys[k], *vals[k], d);
            sp->loadFxForTrack ("__master", k, d, &st);
        }
        if (sp->engine.getFx (sp->engine.getOrCreateSlot ("__master"), 3) != nullptr)
        {
            sp->engine.setTrackFx (sp->engine.getOrCreateSlot ("__master"), 3, nullptr);
            sp->fxDesc["__master"].erase (3);
            sp->sendTrackPlugins();
            sp->saveSession();
        }
        put (o, "before", std::round (j.lufsBefore * 10.0) / 10.0); put (o, "after", std::round (j.lufsAfter * 10.0) / 10.0);
        put (o, "eqLow", std::round (j.eqLow * 10.0f) / 10.0f); put (o, "eqHigh", std::round (j.eqHigh * 10.0f) / 10.0f);
        put (o, "thr", std::round (j.compThr * 10.0f) / 10.0f); put (o, "gain", std::round (j.limGain * 10.0f) / 10.0f);
        sp->emit ("mastered", o);
    });
    job->launchThread();
}

namespace
{
    struct AnalyzeJob : juce::ThreadWithProgressWindow
    {
        AnalyzeJob (juce::AudioBuffer<float> b, double s, int w, std::function<void (bool, AnalysisResult&)> onDone)
            : ThreadWithProgressWindow (sk ("Analyzujem nahrávku"), true, true), buf (std::move (b)), sr (s), what (w), done (std::move (onDone)) {}
        void run() override
        {
            setStatusMessage (sk ("Hľadám doby, tempo, akordy a údery…"));
            res = analyzeAudio (buf, sr, what, [this] (double p) { setProgress (p); return ! threadShouldExit(); });
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (res.ok && ! cancelled, res);
            delete this;
        }
        juce::AudioBuffer<float> buf;
        double sr;
        int what;
        AnalysisResult res;
        std::function<void (bool, AnalysisResult&)> done;
    };
}

void MainView::onRefLoad (const juce::String& path)
{
    juce::Component::SafePointer<MainView> sp (this);
    auto run = [sp] (juce::File f)
    {
        if (sp == nullptr || ! f.existsAsFile()) return;
        const auto id = sp->store->importFile (f);
        auto b = id.isNotEmpty() ? sp->store->get (id) : nullptr;
        auto o = obj();
        if (b == nullptr) { put (o, "error", sk ("Súbor sa nedá prečítať.")); sp->emit ("refLoaded", o); return; }
        const double lufs = measureLufs (*b, sp->store->getSampleRate());
        sp->engine.setReference (b, 1.0f);
        put (o, "id", id); put (o, "name", f.getFileNameWithoutExtension()); put (o, "lufs", std::round (lufs * 10.0) / 10.0);
        sp->emit ("refLoaded", o);
    };
    if (juce::File::isAbsolutePath (path)) { run (juce::File (path)); return; }
    chooser = std::make_unique<juce::FileChooser> (sk ("Referenčná pesnička"), juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [run] (const juce::FileChooser& fc) { run (fc.getResult()); });
}

namespace
{
    struct MixLufsJob : juce::ThreadWithProgressWindow
    {
        MixLufsJob (Engine& e, std::unique_ptr<SongData> s, double endStep, std::function<void (bool, double)> onDone)
            : ThreadWithProgressWindow (sk ("Meriam hlasitosť mixu"), true, true), engine (e), sd (std::move (s)), end (endStep), done (std::move (onDone)) {}
        void run() override
        {
            const double sr = engine.getSampleRate();
            const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_ref.wav");
            tmp.deleteFile();
            {
                std::unique_ptr<juce::OutputStream> os (tmp.createOutputStream().release());
                if (os == nullptr) return;
                juce::WavAudioFormat wav;
                auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (32)
                                                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                if (w == nullptr) return;
                if (! engine.renderOffline (*sd, end, 0.5, *w, [this] (double p) { setProgress (p); return ! threadShouldExit(); })) { tmp.deleteFile(); return; }
            }
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (new juce::FileInputStream (tmp), true));
            if (rd == nullptr) return;
            juce::AudioBuffer<float> mix (2, (int) rd->lengthInSamples);
            rd->read (&mix, 0, (int) rd->lengthInSamples, 0, true, true);
            rd.reset();
            tmp.deleteFile();
            lufs = measureLufs (mix, sr);
            ok = true;
        }
        void threadComplete (bool cancelled) override { if (done) done (ok && ! cancelled, lufs); delete this; }
        Engine& engine;
        std::unique_ptr<SongData> sd;
        double end, lufs = 0;
        bool ok = false;
        std::function<void (bool, double)> done;
    };
}

void MainView::onRefMatch (const juce::var& v)
{
    if (exporting.load() || lastSong.isVoid()) return;
    auto sd = parseCurrent (lastSong);
    if (sd == nullptr) return;
    engine.stop();
    exporting = true;
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new MixLufsJob (engine, std::move (sd), juce::jmax (16.0, (double) v.getProperty ("end", 64)), [sp] (bool ok, double lufs)
    {
        if (sp == nullptr) return;
        sp->exporting = false;
        sp->lastSentPos = -1;
        auto o = obj(); put (o, "ok", ok); put (o, "mix", std::round (lufs * 10.0) / 10.0);
        sp->emit ("refMatched", o);
    });
    job->launchThread();
}

void MainView::onAnalyze (const juce::var& v)
{
    const auto req = v["req"].toString();
    auto src = store->get (v["id"].toString());
    auto fail = [this, req] (const juce::String& msg) { auto o = obj(); put (o, "req", req); put (o, "error", msg); emit ("analyzed", o); };
    if (src == nullptr) { fail (sk ("Zvuk klipu sa nenašiel.")); return; }
    const double sr = store->getSampleRate();
    const int a0 = juce::jlimit (0, src->getNumSamples(), (int) ((double) v.getProperty ("off", 0.0) * sr));
    const int len = juce::jlimit (0, src->getNumSamples() - a0, (int) ((double) v.getProperty ("dur", 0.0) * sr));
    if (len < (int) (sr * 4)) { fail (sk ("Klip je príliš krátky (aspoň 4 sekundy).")); return; }
    juce::AudioBuffer<float> clip (juce::jmax (1, src->getNumChannels()), len);
    for (int c = 0; c < clip.getNumChannels(); ++c) clip.copyFrom (c, 0, *src, c, a0, len);
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new AnalyzeJob (std::move (clip), sr, (int) v.getProperty ("what", 1), [sp, req] (bool ok, AnalysisResult& r)
    {
        if (sp == nullptr) return;
        auto o = obj(); put (o, "req", req);
        if (! ok) { put (o, "error", r.beats.size() < 8 && r.bpm <= 0.0 ? sk ("V nahrávke sa nenašiel zreteľný rytmus.") : juce::String ("cancel")); sp->emit ("analyzed", o); return; }
        auto r3 = [] (double x) { return std::round (x * 1000.0) / 1000.0; };
        put (o, "bpm", std::round (r.bpm * 100.0) / 100.0); put (o, "steady", r.steady); put (o, "downbeat", r.downbeat);
        juce::Array<juce::var> beats, chords;
        for (double b : r.beats) beats.add (r3 (b));
        for (auto& c : r.chords) { auto q = obj(); put (q, "t", r3 (c.t)); put (q, "c", c.name); chords.add (q); }
        put (o, "beats", beats); put (o, "chords", chords);
        auto hits = [&] (const std::vector<AnalysisResult::Hit>& h) { juce::Array<juce::var> a; for (auto& x : h) { juce::Array<juce::var> p; p.add (r3 (x.t)); p.add (x.v); a.add (p); } return a; };
        put (o, "kick", hits (r.kick)); put (o, "snare", hits (r.snare)); put (o, "hat", hits (r.hat));
        sp->emit ("analyzed", o);
    });
    job->launchThread();
}

//==============================================================================
// import MIDI: noty po stopach a kanaloch, v krokoch (1/16)
juce::var readMidiFile (const juce::File& f)
{
    auto res = obj();
    put (res, "file", f.getFileNameWithoutExtension());
    put (res, "path", f.getFullPathName());
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
    if ((bool) v.getProperty ("freeze", false)) t.group = -1;   // zmrazenie: bez skupiny (skupina sa prida pri prehravani)
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
    if (which == 0)
    {
        inspLast.erase (track);
        for (auto* pr : p->getParameters())
            if (auto* hp = dynamic_cast<juce::AudioPluginInstance::HostedParameter*> (pr))
            {
                // VST3 meni ID parametra na cislo (hash); skusime holy text aj obe varianty hashu
                const auto id = hp->getParameterID();
                if (id == "capture_seq" || id == juce::String (juce::VST3ClientExtensions::convertJuceParameterId ("capture_seq", true))
                    || id == juce::String (juce::VST3ClientExtensions::convertJuceParameterId ("capture_seq", false)))
                    w->captureIdx = hp->getParameterIndex();
                if (id == "learn_req" || id == juce::String (juce::VST3ClientExtensions::convertJuceParameterId ("learn_req", true))
                    || id == juce::String (juce::VST3ClientExtensions::convertJuceParameterId ("learn_req", false)))
                    w->learnIdx = hp->getParameterIndex();
                if (id == "vc_src" || id == juce::String (juce::VST3ClientExtensions::convertJuceParameterId ("vc_src", true))
                    || id == juce::String (juce::VST3ClientExtensions::convertJuceParameterId ("vc_src", false)))
                    w->vocSrcIdx = hp->getParameterIndex();
            }
    }
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

// "Inspiracia": Korgus Inspire zvysil capture_seq -> precitame <capture> zo stavu a posleme frazu do rozhrania
void MainView::vocSrcChanged (const juce::String& track, float v01)
{
    const int idx = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v01) * 32.0f);
    juce::Component::SafePointer<MainView> sp (this);
    juce::MessageManager::callAsync ([sp, track, idx]
    {
        if (sp == nullptr) return;
        auto o = obj(); put (o, "track", track); put (o, "idx", idx);
        sp->emit ("vocSrc", o);
    });
}

void MainView::inspireLearnReq (const juce::String& track)
{
    juce::Component::SafePointer<MainView> sp (this);
    juce::MessageManager::callAsync ([sp, track] { if (sp != nullptr) { auto o = obj(); o.getDynamicObject()->setProperty ("track", track); sp->emit ("learnReq", o); } });
}

void MainView::inspireCaptured (const juce::String& track)
{
    juce::Component::SafePointer<MainView> sp (this);
    juce::MessageManager::callAsync ([sp, track] { if (sp != nullptr) sp->onInspireCapture (track); });
}

void MainView::onInspireCapture (const juce::String& track)
{
    auto* p = engine.getPlugin (engine.getOrCreateSlot (track));
    if (p == nullptr) return;
    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    const auto tree = SoundMatch::decodeInspireState (mb);
    const auto cap = tree.getChildWithName ("capture");
    if (! cap.isValid()) return;
    const int id = (int) cap.getProperty ("id", 0);
    if (id <= 0 || id <= inspLast[track]) return;
    inspLast[track] = id;
    // v Sequenceri berieme len to, co hrac naozaj zahral (plugin vidi aj noty z prehravania piesne)
    double ppq = -1.0;
    auto notes = livePhrase (track, ppq);
    auto o = obj();
    put (o, "track", track); put (o, "id", id); put (o, "bpm", 0.0); put (o, "ppq", ppq);
    put (o, "notes", notes);
    emit ("inspireCapture", o);
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
        SeparateJob (juce::AudioBuffer<float> clip, double sr, juce::File dataAi, juce::File exeAi, bool fourStems,
                     std::function<void (bool, juce::String, std::vector<juce::AudioBuffer<float>>&)> onDone)
            : ThreadWithProgressWindow (sk (fourStems ? "Rozkladám pesničku na stopy" : "Oddeľujem spev a hudbu"), true, true),
              mix (std::move (clip)), rate (sr), dataDir (dataAi), exeDir (exeAi), four (fourStems), done (std::move (onDone)) {}

        juce::File find (const juce::String& name) const
        {
            if (exeDir.getChildFile (name).existsAsFile()) return exeDir.getChildFile (name);
            return dataDir.getChildFile (name);
        }
        bool ensureModel (const juce::String& name, const juce::String& url, const juce::String& sizeTxt, juce::File& out,
                          const std::function<bool (double)>& prog)
        {
            out = find (name);
            if (out.existsAsFile()) return true;
            setStatusMessage (sk ("Sťahujem model ") + name + " (" + sizeTxt + sk (") – len prvý raz…"));
            const auto tmp = out.getSiblingFile (name + ".part");
            if (! downloadFile (url, tmp, error, prog)) { tmp.deleteFile(); return false; }
            return tmp.moveFileTo (out);
        }
        void run() override
        {
            auto prog = [this] (double p) { setProgress (p); return ! threadShouldExit(); };
            juce::File model, mDrums, mBass;
            if (! ensureModel ("Kim_Vocal_2.onnx", modelUrl, "66 MB", model, prog)) return;
            if (four)
            {
                const juce::String base ("https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/");
                if (! ensureModel ("kuielab_a_drums.onnx", base + "kuielab_a_drums.onnx", "30 MB", mDrums, prog)) return;
                if (! ensureModel ("kuielab_a_bass.onnx", base + "kuielab_a_bass.onnx", "30 MB", mBass, prog)) return;
            }
            auto ort = find (ortName);
            if (! ort.existsAsFile())
            {
               #if JUCE_WINDOWS
                setStatusMessage (sk ("Sťahujem ONNX Runtime (65 MB) – len prvý raz…"));
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
                if (! ort.existsAsFile()) { error = sk ("Chýba ONNX Runtime: ") + ort.getFullPathName(); return; }
            }
            auto in44 = resampleTo (mix, rate, 44100.0);
            if (in44.getNumChannels() < 2) { in44.setSize (2, in44.getNumSamples(), true); in44.copyFrom (1, 0, in44, 0, 0, in44.getNumSamples()); }
            const int n = in44.getNumSamples();
            const int steps = four ? 3 : 1;
            auto part = [this, steps] (int k) { return [this, k, steps] (double p) { setProgress ((k + p) / steps); return ! threadShouldExit(); }; };

            setStatusMessage (sk (four ? "Spev… (1/3)" : "Oddeľujem spev a hudbu… (pesnička 3 min ≈ 1 – 3 min)"));
            setProgress (0.0);
            juce::AudioBuffer<float> voc44;
            if (! separateVocals (ort, model, in44, voc44, error, part (0))) return;
            juce::AudioBuffer<float> rest (2, n);   // zvysok po odcitani
            for (int c = 0; c < 2; ++c) { rest.copyFrom (c, 0, in44, c, 0, n); rest.addFrom (c, 0, voc44, c, 0, n, -1.0f); }
            outs.push_back (resampleTo (voc44, 44100.0, rate));
            if (four)
            {
                juce::AudioBuffer<float> dr44, bs44;
                setStatusMessage (sk ("Bicie… (2/3)"));
                if (! separateMdx (ort, mDrums, MdxParams { 4096, 2048, 512, 1.0f }, in44, dr44, error, part (1))) return;
                setStatusMessage (sk ("Basa… (3/3)"));
                if (! separateMdx (ort, mBass, MdxParams { 16384, 2048, 512, 1.0f }, in44, bs44, error, part (2))) return;
                for (int c = 0; c < 2; ++c) { rest.addFrom (c, 0, dr44, c, 0, n, -1.0f); rest.addFrom (c, 0, bs44, c, 0, n, -1.0f); }
                outs.push_back (resampleTo (dr44, 44100.0, rate));
                outs.push_back (resampleTo (bs44, 44100.0, rate));
            }
            outs.push_back (resampleTo (rest, 44100.0, rate));
            ok = true;
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (ok && ! cancelled, cancelled ? sk ("Zrušené.") : error, outs);
            delete this;
        }

        juce::AudioBuffer<float> mix;
        std::vector<juce::AudioBuffer<float>> outs;   // spev, [bicie, basa,] zvysok
        double rate;
        juce::File dataDir, exeDir;
        bool four = false;
        std::function<void (bool, juce::String, std::vector<juce::AudioBuffer<float>>&)> done;
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
            : ThreadWithProgressWindow (sk ("Dolaďujem spev"), true, true),
              src (std::move (clip)), rate (sr), key (k), scale (s), speed (sp), done (std::move (onDone)) {}

        void run() override
        {
            setStatusMessage (sk ("Dolaďujem spev…"));
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

namespace
{
    struct DenoiseJob : juce::ThreadWithProgressWindow
    {
        DenoiseJob (juce::AudioBuffer<float> clip, double sr, float amt, int ps, int pl,
                    std::function<void (bool, juce::AudioBuffer<float>&)> onDone)
            : ThreadWithProgressWindow (sk ("Odšumujem nahrávku"), true, true),
              src (std::move (clip)), rate (sr), amount (amt), pStart (ps), pLen (pl), done (std::move (onDone)) {}

        void run() override
        {
            setStatusMessage (sk ("Odšumujem nahrávku…"));
            ok = denoise (src, rate, amount, pStart, pLen, out, [this] (double p) { setProgress (p); return ! threadShouldExit(); });
        }
        void threadComplete (bool cancelled) override
        {
            if (done) done (ok && ! cancelled, out);
            delete this;
        }

        juce::AudioBuffer<float> src, out;
        double rate;
        float amount;
        int pStart, pLen;
        bool ok = false;
        std::function<void (bool, juce::AudioBuffer<float>&)> done;
    };
}

// prehliadac zvukov: obsah priecinka alebo vyhladavanie v nom (aj v podpriecinkoch)
// predvolena kniznica zvukov bicich: supravy z DrumKits ulozene ako WAV (Hudba\Korgus Bicie\<suprava>\NN nazov.wav)
static juce::File korgusDrumLib()
{
    const auto root = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Korgus Bicie");
    const auto mark = root.getChildFile (".korgus_v3");
    if (mark.existsAsFile()) return root;
    if (! root.createDirectory()) return {};
    // stare supravy (v1) zmazat - nahradili ich Pop, House, Drum & Bass, Afro, Latino
    for (auto* old : { "Akustické", "808 / Trap", "Elektronické (909)", "Perkusie", "Ľudová" })
        if (auto d = root.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (old))); d.isDirectory()) d.deleteRecursively();
    root.getChildFile (".korgus_v1").deleteFile();
    root.getChildFile (".korgus_v2").deleteFile();
    // v3: Pop a House su zo samplov - stare syntetizovane subory v nich zmazat
    for (auto* old : { "Pop", "House" })
        if (auto d = root.getChildFile (old); d.isDirectory()) d.deleteRecursively();
    constexpr double sr = 44100.0;
    for (int k = 0; k < DrumKits::numKits; ++k)
    {
        const auto kd = root.getChildFile (juce::File::createLegalFileName (DrumKits::kitName (k)));
        kd.createDirectory();
        for (int l = 0; l < DrumKits::numLanes; ++l)
        {
            juce::AudioBuffer<float> b;
            if (! DrumKits::make (k, l, sr, b)) continue;
            const auto f = kd.getChildFile (juce::String (l + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (DrumKits::laneName (k, l)) + ".wav");
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            if (os == nullptr) continue;
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
            if (w == nullptr) continue;
            const float* ch[] = { b.getReadPointer (0) };
            w->writeFromFloatArrays (ch, 1, b.getNumSamples());
        }
    }
    mark.replaceWithText ("1");
    return root;
}

void MainView::onFsList (const juce::var& v)
{
    const auto* extArr = v["ext"].getArray();
    const bool drumKind = v["kind"].toString().isEmpty() && (extArr == nullptr || extArr->isEmpty());
    const auto drumLib = drumKind ? korgusDrumLib() : juce::File();
    juce::StringArray exts { "wav", "mp3", "ogg", "flac", "aif", "aiff" };
    if (const auto* ea = v["ext"].getArray(); ea != nullptr && ! ea->isEmpty())
    {
        exts.clear();
        for (auto& e : *ea) exts.add (e.toString().toLowerCase());
    }
    auto isAudio = [&exts] (const juce::File& f) { return exts.contains (f.getFileExtension().substring (1).toLowerCase()); };
    juce::File dir (v["path"].toString());
    auto spliceDir = []
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        for (auto f : { home.getChildFile ("Splice").getChildFile ("sounds"), home.getChildFile ("Splice"),
                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Splice") })
            if (f.isDirectory()) return f;
        return juce::File();
    };
    if (! juce::File::isAbsolutePath (v["path"].toString()) || ! dir.isDirectory())
        dir = (v["kind"].toString() == "loop" && spliceDir().isDirectory()) ? spliceDir()
            : (drumLib.isDirectory() ? drumLib : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    const auto q = v["q"].toString().trim();
    auto o = obj();
    put (o, "req", v["req"]); put (o, "path", dir.getFullPathName()); put (o, "q", q);
    put (o, "parent", dir.getParentDirectory() != dir ? dir.getParentDirectory().getFullPathName() : juce::String());
    juce::Array<juce::var> dirs, files;
    auto fileObj = [&] (const juce::File& f, const juce::String& shown)
    {
        auto e = obj(); put (e, "n", shown); put (e, "p", f.getFullPathName()); put (e, "sz", (double) f.getSize());
        files.add (e);
    };
    bool more = false;
    if (q.isEmpty())
    {
        auto subs = dir.findChildFiles (juce::File::findDirectories, false);
        subs.sort();
        for (auto& d : subs)
        {
            if (d.isHidden() || d.getFileName().startsWithChar ('.') || d.getFileName().startsWithChar ('$')) continue;
            auto e = obj(); put (e, "n", d.getFileName()); put (e, "p", d.getFullPathName()); dirs.add (e);
        }
        auto fs = dir.findChildFiles (juce::File::findFiles, false);
        fs.sort();
        for (auto& f : fs) if (isAudio (f) && ! f.isHidden()) { if (files.size() >= 2000) { more = true; break; } fileObj (f, f.getFileName()); }
    }
    else
    {
        // vsetky slova musia byt v nazve (alebo v ceste pod priecinkom)
        juce::StringArray words; words.addTokens (q.toLowerCase(), " ", "");
        words.removeEmptyStrings();
        int scanned = 0;
        for (const auto& en : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
        {
            if (++scanned > 60000) { more = true; break; }
            const auto f = en.getFile();
            if (! isAudio (f)) continue;
            const auto rel = f.getRelativePathFrom (dir), low = rel.toLowerCase();
            bool okAll = true;
            for (auto& w : words) if (! low.contains (w)) { okAll = false; break; }
            if (! okAll) continue;
            if (files.size() >= 500) { more = true; break; }
            fileObj (f, rel);
        }
    }
    put (o, "dirs", dirs); put (o, "files", files); put (o, "more", more);
    // jednotky a bezne miesta
    juce::Array<juce::var> places;
    auto place = [&] (const juce::String& n, const juce::File& f) { if (f.isDirectory()) { auto e = obj(); put (e, "n", n); put (e, "p", f.getFullPathName()); places.add (e); } };
    if (drumLib.isDirectory()) place (sk ("Bicie Korgus"), drumLib);
    place (sk ("Hudba"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    place (sk ("Dokumenty"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    place (sk ("Plocha"), juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
    place (sk ("Stiahnuté"), juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));
    if (spliceDir().isDirectory()) place (sk ("Slučky"), spliceDir());
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);
    for (auto& r : roots) place (r.getFullPathName(), r);
    put (o, "places", places);
    emit ("fsList", o);
}

void MainView::onDenoise (const juce::var& v)
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
    const double p0 = (double) v.getProperty ("p0", -1.0), p1 = (double) v.getProperty ("p1", -1.0);
    int ps = 0, pl = 0;
    if (p0 >= 0.0 && p1 > p0)
    {
        ps = juce::jlimit (0, len, (int) (p0 * sr));
        pl = juce::jlimit (0, len - ps, (int) ((p1 - p0) * sr));
        if (pl < 2048) { fail (sk ("Úsek so šumom medzi lokátormi je príliš krátky (treba aspoň 0,1 s v klipe).")); return; }
    }
    juce::Component::SafePointer<MainView> sp (this);
    auto* job = new DenoiseJob (std::move (clip), sr, (float) (double) v.getProperty ("amount", 12.0), ps, pl,
        [sp, req, sr] (bool ok, juce::AudioBuffer<float>& out)
        {
            if (sp == nullptr) return;
            auto o = obj(); put (o, "req", req);
            if (! ok) { put (o, "error", juce::String ("cancel")); sp->emit ("clipResult", o); return; }
            const auto nid = sp->store->addRecording (out.getReadPointer (0), out.getNumChannels() > 1 ? out.getReadPointer (1) : nullptr,
                                                      out.getNumSamples(), sr);
            if (nid.isEmpty()) { put (o, "error", sk ("Výsledok sa nepodarilo uložiť.")); sp->emit ("clipResult", o); return; }
            sp->emit ("audioPeaks", sp->store->peaksFor (nid));
            put (o, "id", nid); put (o, "dur", out.getNumSamples() / sr);
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
    const bool four = (bool) v.getProperty ("four", false);
    auto* job = new SeparateJob (std::move (clip), sr, dataAi, exeAi, four,
        [sp, req, sr] (bool ok, juce::String err, std::vector<juce::AudioBuffer<float>>& outs)
        {
            if (sp == nullptr) return;
            auto o = obj(); put (o, "req", req);
            if (! ok) { put (o, "error", err); sp->emit ("separated", o); return; }
            juce::Array<juce::var> ids;
            for (auto& b : outs)
            {
                const auto id = sp->store->addRecording (b.getReadPointer (0), b.getReadPointer (1), b.getNumSamples(), sr);
                if (id.isEmpty()) { put (o, "error", sk ("Výsledok sa nepodarilo uložiť.")); sp->emit ("separated", o); return; }
                sp->emit ("audioPeaks", sp->store->peaksFor (id));
                ids.add (id);
            }
            put (o, "ids", ids);
            if (ids.size() == 2) { put (o, "voc", ids[0]); put (o, "mus", ids[1]); }
            put (o, "dur", outs.empty() ? 0.0 : outs[0].getNumSamples() / sr);
            sp->emit ("separated", o);
        });
    job->launchThread();
}

//==============================================================================
// automaticke aktualizacie: verejny repozitar Korgus7/Korgus-Download, subor latest.json
// {"version":"1.0.N","url":"https://.../KorgusSequencer-Setup.exe","notes":"...","notes_en":"..."}
namespace
{
    const char* updApi = "https://raw.githubusercontent.com/Korgus7/Korgus-Download/main/latest.json";

    juce::Array<int> verParts (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::Array<int> r;
        for (const auto& t : juce::StringArray::fromTokens (v, ".", "")) r.add (t.getIntValue());
        while (r.size() < 3) r.add (0);
        return r;
    }
    bool verNewer (const juce::String& a, const juce::String& b)   // a > b
    {
        const auto x = verParts (a), y = verParts (b);
        for (int i = 0; i < juce::jmax (x.size(), y.size()); ++i)
        {
            const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
            if (p != q) return p > q;
        }
        return false;
    }
}

void MainView::checkForUpdate()
{
   #if JUCE_WINDOWS
    juce::Component::SafePointer<MainView> sp (this);
    const auto cur = currentVersion();
    juce::Thread::launch ([sp, cur]
    {
        // GitHub (raw) drzi subory v cache az 5 minut - parameter ?t= zaruci cerstvu verziu
        juce::URL u (juce::String (updApi) + "?t=" + juce::String (juce::Time::currentTimeMillis()));
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (8000)
                        .withExtraHeaders ("User-Agent: KorgusSequencer\r\nCache-Control: no-cache");
        auto in = u.createInputStream (opts);
        if (in == nullptr) return;
        const auto j = juce::JSON::parse (in->readEntireStreamAsString());
        const auto tag = j["version"].toString();
        if (tag.isEmpty() || ! verNewer (tag, cur)) return;
        const auto url = j["url"].toString();
        if (! url.startsWith ("https://")) return;
        // popis novej verzie v jazyku programu (notes_en pre anglictinu, inak slovensky)
        const auto notesEn = j["notes_en"].toString();
        const auto notes = (appLang() == 1 && notesEn.isNotEmpty()) ? notesEn : j["notes"].toString();
        const auto files = j["files"];
        juce::MessageManager::callAsync ([sp, tag, url, notes, files]
        {
            if (sp == nullptr) return;
            sp->updUrl = url; sp->updVer = tag.trimCharactersAtStart ("vV"); sp->updFiles = files;
            auto o = obj();
            put (o, "ver", sp->updVer);
            put (o, "cur", sp->currentVersion());
            put (o, "notes", notes);
            sp->emit ("update", o);
        });
    });
   #endif
}

void MainView::onUpdateNow()
{
   #if JUCE_WINDOWS
    if (updUrl.isEmpty() || updBusy.exchange (true)) return;
    if (updFiles.isArray() && updFiles.size() > 0) { updateByFiles(); return; }
    juce::Component::SafePointer<MainView> sp (this);
    const auto url = updUrl, ver = updVer;
    juce::Thread::launch ([sp, url, ver]
    {
        auto fail = [sp] (const juce::String& msg)
        {
            juce::MessageManager::callAsync ([sp, msg] { if (sp == nullptr) return; sp->updBusy = false; auto o = obj(); put (o, "error", msg); sp->emit ("updateProgress", o); });
        };
        const auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KorgusSequencer-Setup-" + ver + ".exe");
        int status = 0;
        auto in = juce::URL (url + (url.containsChar ('?') ? "&t=" : "?t=") + juce::String (juce::Time::currentTimeMillis())).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (15000).withExtraHeaders ("User-Agent: KorgusSequencer").withNumRedirectsToFollow (8)
                        .withStatusCode (&status));
        if (in == nullptr || (status != 0 && status >= 400)) { fail (sk ("Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.")); return; }
        dest.deleteFile();
        bool truncated = false;
        {
            juce::FileOutputStream out (dest);
            if (! out.openedOk()) { fail (sk ("Aktualizáciu sa nepodarilo uložiť.")); return; }
            const auto total = in->getTotalLength();
            juce::HeapBlock<char> buf (1 << 16);
            juce::int64 done = 0; int lastPct = -1;
            for (;;)
            {
                const int n = in->read (buf, 1 << 16);
                if (n <= 0) break;
                out.write (buf, (size_t) n); done += n;
                const int pct = total > 0 ? (int) (done * 100 / total) : -1;
                if (pct != lastPct && pct >= 0)
                {
                    lastPct = pct;
                    juce::MessageManager::callAsync ([sp, pct] { if (sp == nullptr) return; auto o = obj(); put (o, "pct", pct); sp->emit ("updateProgress", o); });
                }
            }
            out.flush();
            if (total > 0 && done != total) truncated = true;
        }
        if (truncated || dest.getSize() < 1000000) { dest.deleteFile(); fail (sk ("Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.")); return; }
        juce::MessageManager::callAsync ([sp, dest]
        {
            if (sp == nullptr) return;
            auto o = obj(); put (o, "pct", 100); put (o, "done", true); sp->emit ("updateProgress", o);
            // tichá inštalácia, po skončení inštalátor program znova spustí
            if (dest.startAsProcess ("/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS"))
                juce::Timer::callAfterDelay (600, [] { if (auto* app = juce::JUCEApplication::getInstance()) app->systemRequestedQuit(); });
            else { sp->updBusy = false; auto e = obj(); put (e, "error", sk ("Inštalátor sa nepodarilo spustiť.")); sp->emit ("updateProgress", e); }
        });
    });
   #endif
}

// aktualizacia bez instalatora: stiahne len zmenene subory (exe, vst3), overi ich
// (velkost, SHA-256, ci ich Windows pusti) a az potom ich vymeni; stare ostanu ako *.old
void MainView::updateByFiles()
{
   #if JUCE_WINDOWS
    juce::Component::SafePointer<MainView> sp (this);
    const auto files = updFiles;
    const auto ver = updVer;
    juce::Thread::launch ([sp, files, ver]
    {
        auto fail = [sp] (const juce::String& msg)
        {
            juce::MessageManager::callAsync ([sp, msg] { if (sp == nullptr) return; sp->updBusy = false; auto o = obj(); put (o, "error", msg); sp->emit ("updateProgress", o); });
        };
        auto progress = [sp] (int pct)
        {
            juce::MessageManager::callAsync ([sp, pct] { if (sp == nullptr) return; auto o = obj(); put (o, "pct", pct); sp->emit ("updateProgress", o); });
        };
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto dir = exe.getParentDirectory();
        const auto stage = dir.getChildFile ("_update");
        stage.deleteRecursively();
        if (! stage.createDirectory()) { fail (sk ("Aktualizáciu sa nepodarilo uložiť.")); return; }

        struct Item { juce::String name, url, sha; juce::int64 size; };
        std::vector<Item> items;
        juce::int64 total = 0;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto& f = files[i];
            Item it { f["name"].toString(), f["url"].toString(), f["sha256"].toString().toLowerCase(), (juce::int64) f["size"] };
            // len obycajne nazvy suborov v priecinku programu
            if (it.name.isEmpty() || it.name.containsAnyOf ("\\/:*?\"<>|") || it.name.startsWith (".")
                || ! it.url.startsWith ("https://") || it.sha.length() != 64 || it.size <= 0) { fail (sk ("Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.")); return; }
            items.push_back (it); total += it.size;
        }

        // 1) stiahnutie a kontrola
        juce::int64 doneAll = 0; int lastPct = -1;
        for (const auto& it : items)
        {
            const auto dest = stage.getChildFile (it.name);
            int status = 0;
            auto in = juce::URL (it.url + (it.url.containsChar ('?') ? "&t=" : "?t=") + juce::String (juce::Time::currentTimeMillis()))
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                              .withConnectionTimeoutMs (15000).withExtraHeaders ("User-Agent: KorgusSequencer").withNumRedirectsToFollow (8).withStatusCode (&status));
            if (in == nullptr || (status != 0 && status >= 400)) { stage.deleteRecursively(); fail (sk ("Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.")); return; }
            {
                juce::FileOutputStream out (dest);
                if (! out.openedOk()) { stage.deleteRecursively(); fail (sk ("Aktualizáciu sa nepodarilo uložiť.")); return; }
                juce::HeapBlock<char> buf (1 << 16);
                for (;;)
                {
                    const int n = in->read (buf, 1 << 16);
                    if (n <= 0) break;
                    out.write (buf, (size_t) n); doneAll += n;
                    const int pct = (int) juce::jlimit<juce::int64> (0, 95, doneAll * 95 / juce::jmax<juce::int64> (1, total));
                    if (pct != lastPct) { lastPct = pct; progress (pct); }
                }
                out.flush();
            }
            if (dest.getSize() != it.size || juce::SHA256 (dest).toHexString().toLowerCase() != it.sha)
            { stage.deleteRecursively(); fail (sk ("Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.")); return; }
        }

        // 2) pusti ich Windows? (Inteligentna kontrola aplikacii) - skusi ich este pred vymenou
        for (const auto& it : items)
        {
            const auto f = stage.getChildFile (it.name);
            if (it.name.endsWithIgnoreCase (".exe"))
            {
                juce::ChildProcess cp;
                if (! cp.start (juce::StringArray { f.getFullPathName(), "--selftest" }, 0) || ! cp.waitForProcessToFinish (20000) || cp.getExitCode() != 0)
                { stage.deleteRecursively(); fail (sk ("Windows (Inteligentná kontrola aplikácií) nepustil novú verziu. Ostáva ti doterajšia verzia.")); return; }
            }
            else if (it.name.endsWithIgnoreCase (".vst3") || it.name.endsWithIgnoreCase (".dll"))
            {
                juce::DynamicLibrary lib;
                if (! lib.open (f.getFullPathName()))
                { stage.deleteRecursively(); fail (sk ("Windows (Inteligentná kontrola aplikácií) nepustil novú verziu. Ostáva ti doterajšia verzia.")); return; }
                lib.close();
            }
        }
        progress (97);

        // 3) vymena: povodne subory premenovat na *.old (ide aj pri beziacom programe), nove presunut na ich miesto
        std::vector<std::pair<juce::File, juce::File>> moved;   // (cielovy subor, jeho .old)
        auto rollback = [&moved, &stage]
        {
            for (auto it = moved.rbegin(); it != moved.rend(); ++it)
            {
                if (it->first.existsAsFile()) it->first.moveFileTo (stage.getChildFile (it->first.getFileName() + ".new"));
                if (it->second.existsAsFile()) it->second.moveFileTo (it->first);
            }
        };
        for (const auto& it : items)
        {
            const auto target = dir.getChildFile (it.name), old = dir.getChildFile (it.name + ".old");
            old.deleteFile();
            if (target.existsAsFile() && ! target.moveFileTo (old)) { rollback(); stage.deleteRecursively(); fail (sk ("Aktualizáciu sa nepodarilo uložiť.")); return; }
            moved.push_back ({ target, old });
            if (! stage.getChildFile (it.name).moveFileTo (target)) { rollback(); stage.deleteRecursively(); fail (sk ("Aktualizáciu sa nepodarilo uložiť.")); return; }
        }

        bool hasUi = false;
        for (const auto& it : items) if (it.name == "korgus-ui.html") hasUi = true;
        if (hasUi) uiVerFile().replaceWithText (ver);
        juce::MessageManager::callAsync ([sp, exe]
        {
            if (sp == nullptr) return;
            auto o = obj(); put (o, "pct", 100); put (o, "done", true); sp->emit ("updateProgress", o);
            juce::ChildProcess cp;
            if (cp.start (juce::StringArray { exe.getFullPathName(), "--updated" }, 0))
                juce::Timer::callAfterDelay (300, [] { if (auto* app = juce::JUCEApplication::getInstance()) app->systemRequestedQuit(); });
            else { sp->updBusy = false; auto e = obj(); put (e, "error", sk ("Novú verziu sa nepodarilo spustiť. Spusti Korgus Sequencer znova.")); sp->emit ("updateProgress", e); }
        });
    });
   #endif
}

//==============================================================================
// ochrana pred padajucimi pluginmi
bool MainView::isHeld (const juce::PluginDescription& d) const
{
    const auto uid = d.createIdentifierString();
    if (crashSkipUid.isNotEmpty() && uid == crashSkipUid) return true;
    for (const auto& b : blockedPlugins)
        if (b.upToFirstOccurrenceOf ("\t", false, false) == uid) return true;
    if (! isOwnPlugin (d) && ! enabledPlugins.contains (uid)) return true;   // cudzi plugin, ktory si pouzivatel nezaskrtol
    return false;
}

void MainView::unblockPlugin (const juce::String& uid)
{
    for (int i = blockedPlugins.size(); --i >= 0;)
        if (blockedPlugins[i].upToFirstOccurrenceOf ("\t", false, false) == uid) blockedPlugins.remove (i);
    props.getUserSettings()->setValue ("blockedPlugins", blockedPlugins.joinIntoString ("\n"));
    auto h = held; held = obj();
    restorePluginStates (h);   // vrati pluginy, ktore boli v piesni odlozene
    sendPluginList();
}

bool MainView::isOwnPlugin (const juce::PluginDescription& d) const
{
    if (BuiltinFx::isBuiltin (d)) return true;
    if (! juce::File::isAbsolutePath (d.fileOrIdentifier)) return false;
    const juce::File f (d.fileOrIdentifier);
    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    return f.getFileName().startsWithIgnoreCase ("Korgus") && (f.getParentDirectory() == exeDir || f.isAChildOf (exeDir));
}

void MainView::holdEntry (const juce::String& trackId, const juce::var& t, int fxIdx)
{
    if (! held.hasProperty (trackId)) put (held, trackId, obj());
    auto e = held[juce::Identifier (trackId)];
    if (fxIdx < 0) { put (e, "desc", t["desc"]); put (e, "state", t["state"]); }
    else
    {
        juce::Array<juce::var> fx;
        if (const auto* a = e["fx"].getArray()) fx = *a;
        auto c = obj(); put (c, "desc", t["desc"]); put (c, "state", t["state"]); put (c, "idx", fxIdx);
        fx.add (c);
        put (e, "fx", fx);
    }
}

void MainView::mergeHeld (juce::var& states) const
{
    auto* hd = held.getDynamicObject();
    if (hd == nullptr) return;
    for (const auto& prop : hd->getProperties())
    {
        const auto id = prop.name.toString();
        if (! states.hasProperty (id)) put (states, id, obj());
        auto t = states[prop.name];
        if (prop.value.hasProperty ("desc") && ! t.hasProperty ("desc")) { put (t, "desc", prop.value["desc"]); put (t, "state", prop.value["state"]); }
        if (const auto* hf = prop.value["fx"].getArray())
        {
            juce::Array<juce::var> fx;
            if (const auto* a = t["fx"].getArray()) fx = *a;
            for (const auto& f : *hf)
            {
                bool used = false;
                for (const auto& x : fx) if ((int) x["idx"] == (int) f["idx"]) used = true;
                if (! used) fx.add (f);
            }
            put (t, "fx", fx);
        }
    }
}

void MainView::sendCrashInfo()
{
    if (! crashed) return;
    crashed = false;
    auto o = obj();
    put (o, "name", crashName); put (o, "uid", crashSkipUid); put (o, "where", crashWhere);
    put (o, "held", held.getDynamicObject() != nullptr && held.getDynamicObject()->getProperties().size() > 0);
    emit ("crashInfo", o);
}

//==============================================================================
// posledne piesne (menu Subor)
void MainView::addRecent (const juce::File& f)
{
    juce::StringArray r;
    r.addLines (props.getUserSettings()->getValue ("recentSongs"));
    r.removeString (f.getFullPathName(), true);
    r.insert (0, f.getFullPathName());
    r.removeEmptyStrings();
    while (r.size() > 8) r.remove (r.size() - 1);
    props.getUserSettings()->setValue ("recentSongs", r.joinIntoString ("\n"));
    sendRecent();
}

void MainView::sendRecent()
{
    juce::StringArray r;
    r.addLines (props.getUserSettings()->getValue ("recentSongs"));
    juce::Array<juce::var> arr;
    for (const auto& p : r)
    {
        const juce::File f (p);
        if (p.isEmpty() || ! f.existsAsFile()) continue;
        auto o = obj(); put (o, "path", p); put (o, "name", f.getFileNameWithoutExtension());
        arr.add (o);
    }
    emit ("recent", juce::var (arr));
}

//==============================================================================
// "Nauc sa zvuk": Inspire napodobni zvuk inej stopy
void MainView::onLearnSound (const juce::var& v)
{
    auto fail = [this] (const juce::String& m) { auto o = obj(); put (o, "ok", false); put (o, "error", m); emit ("learnDone", o); };
    if (learnJob != nullptr) { fail (sk ("Učenie zvuku už prebieha.")); return; }
    const auto target = v["target"].toString(), source = v["source"].toString();
    // zdroj = nahravka z audio stopy (napr. zvuk napodobeny ustami)
    if (v["aid"].toString().isNotEmpty())
    {
        auto dt2 = trackDesc.find (target);
        auto* pt2 = engine.getPlugin (engine.getOrCreateSlot (target));
        auto buf = store != nullptr ? store->get (v["aid"].toString()) : nullptr;
        if (dt2 == trackDesc.end() || pt2 == nullptr) { fail (sk ("Stopa nemá nástroj.")); return; }
        if (buf == nullptr || buf->getNumSamples() == 0) { fail (sk ("Nahrávka sa nedá prečítať.")); return; }
        const double sr = store->getSampleRate();
        const int off = juce::jlimit (0, buf->getNumSamples() - 1, (int) ((double) v.getProperty ("off", 0.0) * sr));
        const int len = juce::jlimit (1, buf->getNumSamples() - off, (int) (juce::jmin (4.0, (double) v.getProperty ("dur", 4.0)) * sr));
        juce::AudioBuffer<float> clip (juce::jmin (2, buf->getNumChannels()), len);
        for (int c = 0; c < clip.getNumChannels(); ++c) clip.copyFrom (c, 0, *buf, c, off, len);
        const float pk = clip.getMagnitude (0, len); if (pk < 1.0e-4f) { fail (sk ("Nahrávka je tichá.")); return; }
        clip.applyGain (0.7f / pk);
        juce::MemoryBlock mt2; pt2->getStateInformation (mt2);
        auto job = std::make_unique<LearnJob>();
        job->src = std::make_unique<ClipInstrument> (clip, sr, ClipInstrument::detectRoot (clip, sr));
        job->ins = createInstance (dt2->second, &mt2, true);
        if (job->ins == nullptr) { fail (sk ("Nástroj sa nepodarilo pripraviť.")); return; }
        for (auto* p : { job->src.get(), job->ins.get() })
        {
            p->releaseResources(); p->setNonRealtime (true); p->enableAllBuses();
            p->setRateAndBufferSizeDetails (SoundMatch::kSR, SoundMatch::kBS); p->prepareToPlay (SoundMatch::kSR, SoundMatch::kBS);
        }
        job->state = mt2; job->allowSample = false; job->srcName = v["name"].toString(); job->sp = this;
        learnPrev = mt2; learnTarget = target; learnJob = std::move (job); learnJob->startThread();
        return;
    }
    auto dt = trackDesc.find (target), ds = trackDesc.find (source);
    auto* pt = engine.getPlugin (engine.getOrCreateSlot (target));
    auto* ps = engine.getPlugin (engine.getOrCreateSlot (source));
    if (dt == trackDesc.end() || ds == trackDesc.end() || pt == nullptr || ps == nullptr) { fail (sk ("Stopa nemá nástroj.")); return; }
    juce::MemoryBlock mt, ms;
    pt->getStateInformation (mt);
    ps->getStateInformation (ms);
    auto job = std::make_unique<LearnJob>();
    job->src = createInstance (ds->second, &ms, true);
    job->ins = createInstance (dt->second, &mt, true);
    if (job->src == nullptr || job->ins == nullptr) { fail (sk ("Nástroj sa nepodarilo pripraviť.")); return; }
    for (auto* p : { job->src.get(), job->ins.get() })
    {
        // cisty restart: createInstance moze mat plugin uz aktivovany na rate zariadenia (VST3 prepareToPlay by sa vratil skoro)
        p->releaseResources();
        p->setNonRealtime (true);
        p->enableAllBuses();
        p->setRateAndBufferSizeDetails (SoundMatch::kSR, SoundMatch::kBS);
        p->prepareToPlay (SoundMatch::kSR, SoundMatch::kBS);
    }
    job->state = mt;
    job->allowSample = (bool) v.getProperty ("sample", true);
    job->srcName = ds->second.name;
    job->sp = this;
    learnPrev = mt; learnTarget = target;
    learnJob = std::move (job);
    learnJob->startThread();
}

void MainView::learnProgressMsg (int pct, const juce::String& msg)
{
    auto o = obj(); put (o, "pct", pct); put (o, "msg", msg);
    emit ("learnProgress", o);
}

void MainView::learnFinished()
{
    if (learnJob == nullptr) return;
    learnJob->stopThread (5000);
    const auto r = learnJob->res;
    learnJob->src = nullptr; learnJob->ins = nullptr;   // pluginy sa rusia na hlavnom vlakne
    learnJob = nullptr;
    auto o = obj();
    put (o, "ok", r.ok);
    put (o, "diag", r.diag);
    if (! r.ok) { put (o, "error", r.error == "cancel" ? juce::String ("cancel") : r.error); emit ("learnDone", o); return; }
    if (auto* p = engine.getPlugin (engine.getOrCreateSlot (learnTarget)))
    {
        p->suspendProcessing (true);
        p->setStateInformation (r.state.getData(), (int) r.state.getSize());
        p->suspendProcessing (false);
    }
    saveSession();
    put (o, "match", juce::roundToInt (juce::jlimit (0.0f, 1.0f, 1.0f - r.finalLoss / 3.0f) * 100.0f));
    put (o, "start", juce::roundToInt (juce::jlimit (0.0f, 1.0f, 1.0f - r.startLoss / 3.0f) * 100.0f));
    put (o, "sample", r.usedSample);
    put (o, "target", learnTarget);
    emit ("learnDone", o);
}
