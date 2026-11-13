#include "UiCommon.h"

// ---------------------------------------------------------------- jazyk (slovencina / anglictina)
#include <unordered_map>
int& appLang() { static int l = 0; return l; }

juce::String skTranslate (const char* utf8)
{
    if (appLang() == 1 && utf8 != nullptr)
    {
        static const std::unordered_map<std::string, const char*> t = {
        { " a zisťujem tempo…", " and detecting the tempo…" },
        { "Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.", "Couldn't download the update. Check your internet connection." },
        { "Aktualizáciu sa nepodarilo uložiť.", "Couldn't save the update." },
        { "Inštalátor sa nepodarilo spustiť.", "Couldn't start the installer." },
        { " krokov", " steps" },
        { " stôp)", " tracks)" },
        { " vzoriek = ", " samples = " },
        { ") – len prvý raz…", ") – first time only…" },
        { "1/4|1/8|1/8 triola|1/8 bodka|1/16|1/16 triola|1/32", "1/4|1/8|1/8 triplet|1/8 dotted|1/16|1/16 triplet|1/32" },
        { "2 hlasy|4 hlasy|6 hlasov", "2 voices|4 voices|6 voices" },
        { "Analyzujem nahrávku", "Analysing the recording" },
        { "Analyzujem…", "Analysing…" },
        { "Balím projekt", "Packing the project" },
        { "Balím zvuk ", "Packing sound " },
        { "Basa… (3/3)", "Bass… (3/3)" },
        { "Basy", "Low" },
        { "Basy Hz", "Low Hz" },
        { "Bicie… (2/3)", "Drums… (2/3)" },
        { "Bounce stopy do audia", "Bounce track to audio" },
        { "Celý zvuk", "Whole sound" },
        { "Chýba ONNX Runtime: ", "ONNX Runtime missing: " },
        { "Chýba kľúč OpenRouter.", "OpenRouter key missing." },
        { "Chýbajúce pluginy", "Missing plugins" },
        { "Citlivosť", "Velocity" },
        { "Dokumenty", "Documents" },
        { "Dolaďujem hlasitosť…", "Fine-tuning the loudness…" },
        { "Dolaďujem spev", "Tuning vocals" },
        { "Dolaďujem spev…", "Tuning vocals…" },
        { "Dorovnávam hlasitosť…", "Matching loudness…" },
        { "Dozvuk", "Reverb" },
        { "Dočasný súbor sa nedá prečítať.", "Can't read the temporary file." },
        { "Dvojhlas|Harmonizér|Oboje", "Doubler|Harmonizer|Both" },
        { "Dĺžka tónu", "Note length" },
        { "Dĺžka vzoru: ", "Pattern length: " },
        { "Export MP3 – ", "Export MP3 – " },
        { "Export stôp", "Export tracks" },
        { "Export stôp – vyber priečinok", "Export tracks – choose a folder" },
        { "Export videa (MP4)", "Export video (MP4)" },
        { "Export videa – ", "Export video – " },
        { "Farba", "Color" },
        { "Frekvencia", "Frequency" },
        { "Hlasitosť", "Volume" },
        { "Hlasy", "Voices" },
        { "Hore|Dole|Hore-dole|Dole-hore|Náhodne|Ako hráš|Akord", "Up|Down|Up-down|Down-up|Random|As played|Chord" },
        { "Hotovo", "Done" },
        { "Hrať", "Play" },
        { "Hudba", "Music" },
        { "Hĺbka", "Depth" },
        { "Hľadám doby, tempo, akordy a údery…", "Finding beats, tempo, chords and hits…" },
        { "Hľadám pluginy vedľa programu…", "Looking for plugins next to the program…" },
        { "Import audio (WAV, MP3)", "Import audio (WAV, MP3)" },
        { "Klip je príliš krátky (aspoň 4 sekundy).", "The clip is too short (at least 4 seconds)." },
        { "Klip je príliš krátky.", "The clip is too short." },
        { "Korgus Sequencer – zbalený projekt\r\n\r\nOtvor ho v programe: Súbor → Otvoriť (súbor .kpack).\r\n\r\n", "Korgus Sequencer – packed project\r\n\r\nOpen it in the program: File → Open (.kpack file).\r\n\r\n" },
        { "Kroky", "Steps" },
        { "Kým držím klávesu", "While key is held" },
        { "Kľúčová nota", "Root note" },
        { "Ladenie", "Tuning" },
        { "MP3: enkóder sa nespustil.", "MP3: the encoder didn't start." },
        { "MP3: nepodporované nastavenie.", "MP3: unsupported setting." },
        { "Mastering asistent", "Mastering assistant" },
        { "Max. stíšenie", "Max. reduction" },
        { "Meriam hlasitosť mixu", "Measuring the mix loudness" },
        { "Mäkký nábeh|Klasika|Neskorý nástup|Hlboká pumpa|Rýchly nábeh|Opačne|Mäkký pokles|Vrchol|Dlhé podržanie|Plynulý nábeh|Tvrdá pumpa|Brána|Plató|Svah|Údolie|Dvojitá", "Soft rise|Classic|Late start|Deep pump|Fast rise|Reverse|Soft fall|Peak|Long hold|Smooth rise|Hard pump|Gate|Plateau|Slope|Valley|Double" },
        { "Na inom počítači ho otvor cez Súbor → Otvoriť.", "On the other computer open it via File → Open." },
        { "Nahrávam pieseň…", "Recording the song…" },
        { "Najlepšia odozva je s typom zariadenia ASIO a malým bufferom (64 – 256). Ak zvuk praská, zväčši buffer. MIDI klávesy zaškrtni v zozname MIDI vstupov.", "The best response is with the ASIO device type and a small buffer (64 – 256). If the sound crackles, increase the buffer. Tick MIDI keyboards in the list of MIDI inputs." },
        { "Nastavenia zvuku", "Audio settings" },
        { "Načítavam ", "Loading " },
        { "Načítavam nástroj: ", "Loading instrument: " },
        { "Načítavam zvuk ", "Loading sound " },
        { "Načítavam zvukové súbory…", "Loading audio files…" },
        { "Načítať zvuk…", "Load sound…" },
        { "Nedá sa zapísať dočasný súbor.", "Can't write the temporary file." },
        { "Nedá sa zapísať: ", "Can't write: " },
        { "Nepodarilo sa spojiť s OpenRouter. Skontroluj internet.", "Couldn't connect to OpenRouter. Check the internet." },
        { "Nábeh", "Attack" },
        { "ONNX Runtime má nesprávnu verziu.", "ONNX Runtime has the wrong version." },
        { "ONNX Runtime sa nedá načítať: ", "Can't load ONNX Runtime: " },
        { "ONNX Runtime: chýba OrtGetApiBase.", "ONNX Runtime: OrtGetApiBase missing." },
        { "Obrázok na pozadie videa", "Background picture for the video" },
        { "Odšumujem nahrávku", "Denoising the recording" },
        { "Odšumujem nahrávku…", "Denoising the recording…" },
        { "Oktávy", "Octaves" },
        { "Oneskorenie", "Delay" },
        { "Opakovanie", "Feedback" },
        { "Otvoriť", "Open" },
        { "Otvoriť pieseň", "Open song" },
        { "Otváram poslednú pieseň…", "Opening the last song…" },
        { "Otváram zvukovú kartu…", "Opening the sound card…" },
        { "Oddeľujem spev a hudbu", "Separating vocals and music" },
        { "Oddeľujem spev a hudbu… (pesnička 3 min ≈ 1 – 3 min)", "Separating vocals and music… (3 min song ≈ 1 – 3 min)" },
        { "Rozkladám pesničku na stopy", "Splitting the song into tracks" },
        { "Spev… (1/3)", "Vocals… (1/3)" },
        { "Pieseň je tichá – nie je čo masterovať.", "The song is silent – nothing to master." },
        { "Plocha", "Desktop" },
        { "Plugin sa nedá načítať", "Can't load the plugin" },
        { "Pluginy a priečinky…", "Plugins and folders…" },
        { "Pluginy – zoznam a priečinky", "Plugins – list and folders" },
        { "Podržať", "Hold" },
        { "Pomer", "Ratio" },
        { "Potrebné VST pluginy (musia byť nainštalované aj na druhom počítači):\r\n- ", "Required VST plugins (must also be installed on the other computer):\r\n- " },
        { "Počúvam mix…", "Listening to the mix…" },
        { "Počúvať sykavky", "Listen to sibilants" },
        { "Prah", "Threshold" },
        { "Pretiahni sem zvuk (wav, mp3…) alebo klikni na „Načítať zvuk…“", "Drag a sound here (wav, mp3…) or click \"Load sound…\"" },
        { "Projekt je zbalený", "The project is packed" },
        { "Projekt nepotrebuje žiadne ďalšie VST pluginy.\r\n", "The project needs no other VST plugins.\r\n" },
        { "Práve prebieha iný export.", "Another export is running." },
        { "Referenčná pesnička", "Reference song" },
        { "Režim", "Mode" },
        { "Riadenie", "Control" },
        { "Rozladenie", "Detune" },
        { "Rytmus", "Rhythm" },
        { "Rýchlosť", "Rate" },
        { "SIDECHAIN – riadi kľúčová stopa", "SIDECHAIN – driven by the key track" },
        { "Sila", "Velocity" },
        { "Smer", "Direction" },
        { "Stiahnuté", "Downloads" },
        { "Stopa ", "Track " },
        { "Stopa sa nenašla.", "Track not found." },
        { "Stredy", "Mid" },
        { "Stredy Hz", "Mid Hz" },
        { "Strop", "Ceiling" },
        { "Stíšenie sykaviek", "Sibilance reduction" },
        { "Synchro", "Sync" },
        { "Súbor nie je platný zbalený projekt.", "The file isn't a valid packed project." },
        { "Súbor sa nedá prečítať ako MIDI.", "Can't read the file as MIDI." },
        { "Súbor sa nedá prečítať.", "Can't read the file." },
        { "Súbor sa nedá zapísať.", "Can't write the file." },
        { "Sťahovanie sa prerušilo.", "The download was interrupted." },
        { "Sťahovanie zlyhalo (", "Download failed (" },
        { "Sťahujem ONNX Runtime (65 MB) – len prvý raz…", "Downloading ONNX Runtime (65 MB) – first time only…" },
        { "Sťahujem model ", "Downloading model " },
        { "Tento súbor sa nedá prečítať.", "This file can't be read." },
        { "Tento typ MIDI súboru (SMPTE čas) nie je podporovaný.", "This type of MIDI file (SMPTE time) isn't supported." },
        { "Tieto pluginy sa na tomto počítači nenašli – nainštaluj ich a nechaj program vyhľadať pluginy:", "These plugins weren't found on this computer – install them and let the program search for plugins:" },
        { "Tlmenie", "Damping" },
        { "Tvar", "Shape" },
        { "Typ:        ", "Type:       " },
        { "Ukladám MP3…", "Saving MP3…" },
        { "Uložiť", "Save" },
        { "Uvoľnenie", "Release" },
        { "V nahrávke sa nenašiel zreteľný rytmus.", "No clear rhythm found in the recording." },
        { "Veľkosť", "Size" },
        { "Veľkosť pluginu", "Plugin size" },
        { "Vlastný", "Own" },
        { "Voľne|1/4|1/8 bodka|1/8|1/8 triola|1/16", "Free|1/4|1/8 dotted|1/8|1/8 triplet|1/16" },
        { "Vyber zvuk pre sampler", "Choose a sound for the sampler" },
        { "Vytváram video (môže to chvíľu trvať)…", "Creating the video (may take a while)…" },
        { "Vzor", "Pattern" },
        { "Vzor vypnutý – hrá každý krok, sila z hry", "Pattern off – plays every step, velocity as played" },
        { "Vzorkovanie: ", "Sample rate: " },
        { "Výsledok sa nepodarilo uložiť.", "Couldn't save the result." },
        { "Výstup", "Output" },
        { "Výstupná latencia: ", "Output latency: " },
        { "Výšky", "High" },
        { "Výšky Hz", "High Hz" },
        { "Z hry|Zo vzoru|Kombinácia", "Played|From pattern|Combined" },
        { "Z hry", "Played" },
        { "Zapisujem súbor…", "Writing the file…" },
        { "Zapnuté", "On" },
        { "Zariadenie: ", "Device: " },
        { "Zbaliť projekt", "Pack project" },
        { "Zisk", "Gain" },
        { "Zrušené.", "Cancelled." },
        { "Zvuk je dlhší ako 2 minúty.", "The sound is longer than 2 minutes." },
        { "Zvuk klipu sa nenašiel.", "The clip's audio wasn't found." },
        { "Zvuk pre bicie (WAV, MP3)", "Sound for drums (WAV, MP3)" },
        { "Zvuková karta: ", "Sound card: " },
        { "Zvukové súbory: ", "Audio files: " },
        { "Zápis sa nepodaril.", "Writing failed." },
        { "Záťaž CPU:  ", "CPU load:   " },
        { "efekty stopy", "track effects" },
        { "hlasitosť ", "volume " },
        { "klik = krok zap/vyp · ťahanie = sila · dolný riadok = oktáva", "click = step on/off · drag = velocity · bottom row = octave" },
        { "piesen", "song" },
        { "Úsek so šumom medzi lokátormi je príliš krátky (treba aspoň 0,1 s v klipe).", "The noise section between the locators is too short (needs at least 0.1 s in the clip)." },
        { "Čas", "Time" },
        { "Šírka", "Width" },
        { "Šírka Q", "Width Q" },
        { "Žiadne zvukové zariadenie nie je otvorené.", "No audio device is open." },
        { "žiadny|−12 oktáva dole|−7 kvinta dole|−5 kvarta dole|+3 malá terca|+4 veľká terca|+5 kvarta|+7 kvinta|+12 oktáva", "none|−12 octave down|−7 fifth down|−5 fourth down|+3 minor third|+4 major third|+5 fourth|+7 fifth|+12 octave" },
        { "−12 oktáva dole|−7 kvinta dole|−5 kvarta dole|+3 malá terca|+4 veľká terca|+5 kvarta|+7 kvinta|+12 oktáva", "−12 octave down|−7 fifth down|−5 fourth down|+3 minor third|+4 major third|+5 fourth|+7 fifth|+12 octave" },
        { "Spúšťam…", "Starting…" },
        { "Zvuková karta: ", "Sound card: " },
        };
        auto it = t.find (utf8);
        if (it != t.end()) return juce::String::fromUTF8 (it->second);
    }
    return juce::String::fromUTF8 (utf8);
}

juce::String sk (const char* utf8) { return skTranslate (utf8); }

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
            label.setText (sk ("Veľkosť pluginu"), juce::dontSendNotification);
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
    : DocumentWindow (sk ("Pluginy – zoznam a priečinky"), Col::steel, DocumentWindow::closeButton),
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

