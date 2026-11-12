#include "PluginEditor.h"

using namespace nova;
using namespace nova::ui;

namespace
{
    constexpr int SW = 780, SH = 372;       // jednoducha verzia
    constexpr int EW = 1322, EH = 886;      // rozsirena verzia
    const juce::Colour arcB (0xff9d93c4);

    struct Tickable { virtual ~Tickable() = default; virtual void tick() = 0; };

    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    // ===================================================================== jednoducha verzia
    struct SimpleView : juce::Component, Tickable
    {
        struct PresetButton : juce::Component
        {
            juce::String name, key; bool selected = false, hover = false; std::function<void()> onClick;
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (selected ? steelHi : (hover ? steel.brighter (0.12f) : steel));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (selected ? juce::Colours::white : text.withAlpha (0.9f));
                g.setFont (juce::FontOptions (14.0f, selected ? juce::Font::bold : juce::Font::plain));
                g.drawFittedText (name, getLocalBounds().reduced (5, 2), juce::Justification::centred, 2);
            }
            void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
            void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
            void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
        };

        SimpleView (NovaEditor& e) : ed (e), p (e.proc)
        {
            for (auto& f : factoryPatches())
            {
                auto b = std::make_unique<PresetButton>();
                b->name = f.name; b->key = "f:" + f.name;
                auto key = b->key;
                b->onClick = [this, key] { p.loadPreset (key); tick(); };
                addAndMakeVisible (*b);
                buttons.push_back (std::move (b));
            }
            for (auto id : { "bright", "attack", "release", "space", "width", "volume" })
            {
                auto k = std::make_unique<Knob> (p.params, id);
                k->label.setFont (juce::FontOptions (14.0f)); k->label.setColour (juce::Label::textColourId, text);
                addAndMakeVisible (*k);
                knobs.push_back (std::move (k));
            }
            ext.setButtonText (u8 ("Rozšírená ▸"));
            ext.setTooltip (u8 ("Úplný editor zvuku: vrstvy, filtre, efekty, arpeggiátor, vlastné zvuky"));
            ext.onClick = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (1); }); };
            addAndMakeVisible (ext);
            setSize (SW, SH);
            tick();
        }
        void tick() override
        {
            for (auto& b : buttons) { const bool s = b->key == p.currentKey; if (s != b->selected) { b->selected = s; b->repaint(); } }
            juce::String st;
            if (p.synth.pianoFailed()) st = u8 ("Klavírne zvuky sa nepodarilo načítať");
            else if (! p.synth.pianoReady()) st = u8 ("Načítavam klavír…");
            else if (! p.currentKey.startsWith ("f:") && p.currentName.isNotEmpty()) st = p.currentName;
            if (st != status) { status = st; repaint(); }
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (bg);
            auto head = getLocalBounds().removeFromTop (44);
            g.setColour (panel); g.fillRect (head);
            g.setColour (text); g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
            g.drawText ("KORGUS NOVA", head.reduced (16, 0), juce::Justification::centredLeft);
            g.setColour (dim); g.setFont (juce::FontOptions (13.0f));
            g.drawText (status, head.reduced (16, 0).withTrimmedRight (130), juce::Justification::centredRight);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (u8 ("Klavír: Salamander Grand Piano V3 – Alexander Holm, licencia CC-BY 3.0"), getLocalBounds().removeFromBottom (22).reduced (16, 0), juce::Justification::centredLeft);
        }
        void resized() override
        {
            ext.setBounds (getWidth() - 16 - 112, 9, 112, 26);
            auto r = getLocalBounds().reduced (16, 0);
            r.removeFromTop (44 + 12);
            const int cols = 7, gap = 6, bh = 44;
            const int bw = (r.getWidth() - gap * (cols - 1)) / cols;
            for (int i = 0; i < (int) buttons.size(); ++i)
                buttons[(size_t) i]->setBounds (r.getX() + (i % cols) * (bw + gap), r.getY() + (i / cols) * (bh + gap), bw, bh);
            r.removeFromTop (2 * (bh + gap) + 10);
            auto kr = r.removeFromTop (150);
            const int kw = kr.getWidth() / 6;
            for (auto& k : knobs) k->setBounds (kr.removeFromLeft (kw).reduced (12, 0));
        }
        NovaEditor& ed; NovaProcessor& p;
        std::vector<std::unique_ptr<PresetButton>> buttons;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::TextButton ext;
        juce::String status;
    };

    // ===================================================================== vrstva A / B
    struct LayerPanel : juce::Component
    {
        LayerPanel (NovaProcessor& pr, int layer) : p (pr), li (layer), pre (layer == 0 ? "a_" : "b_"),
            on (pr.params, pre + "on", layer == 0 ? u8 ("VRSTVA A") : u8 ("VRSTVA B"), layer == 0 ? arc : arcB),
            wave (pr.params, pre + "wave", juce::StringArray::fromTokens (u8 ("Sín|Troj|Píla|Obdĺž|Šum|Pluck|FM|Sampel"), "|", ""), layer == 0 ? steelHi : accentB),
            ftype (pr.params, pre + "ftype"),
            f24 (pr.params, pre + "f24", "24 dB", layer == 0 ? arc : arcB),
            lshape (pr.params, pre + "lshape"), ltgt (pr.params, pre + "ltgt"), ratio (pr.params, pre + "ratio"), src (pr.params, pre + "src"),
            loop (pr.params, pre + "loop", u8 ("Opakovať"), layer == 0 ? arc : arcB)
        {
            addAndMakeVisible (on); addAndMakeVisible (wave); addAndMakeVisible (ftype); addAndMakeVisible (f24);
            addAndMakeVisible (lshape); addAndMakeVisible (ltgt); addAndMakeVisible (ratio); addAndMakeVisible (src); addAndMakeVisible (loop);
            const char* r1[] = { "oct", "semi", "fine", "uni", "spread", "level", "pan" };
            const char* r2[] = { "cut", "reso", "fenv", "fa", "fd", "fs", "fr" };
            const char* r3[] = { "aa", "ad", "as", "ar", "lrate", "ldepth", "ldelay" };
            const char* t1[] = { "Oktáva", "Poltón", "Jemne", "Unison", "Rozlad.", "Úroveň", "Panoráma" };
            const char* t2[] = { "Cutoff", "Rezon.", "Obálka", "Nábeh", "Pokles", "Držanie", "Doznenie" };
            const char* t3[] = { "Nábeh", "Pokles", "Držanie", "Doznenie", "Rýchlosť", "Hĺbka", "Onesk." };
            for (int i = 0; i < 7; ++i)
            {
                row1.push_back (mk (r1[i], u8 (t1[i])));
                row2.push_back (mk (r2[i], u8 (t2[i])));
                row3.push_back (mk (r3[i], u8 (t3[i])));
            }
            color = mk ("color", {});
            root = mk ("root", u8 ("Zákl. tón"));
            load.setButtonText (u8 ("Načítať WAV…"));
            load.setTooltip (u8 ("Vlastný zvuk do vrstvy (alebo pretiahni WAV súbor myšou na vrstvu)"));
            load.onClick = [this] { chooseFile(); };
            addAndMakeVisible (load);
            smpName.setFont (juce::FontOptions (12.5f)); smpName.setColour (juce::Label::textColourId, dim.brighter (0.3f));
            addAndMakeVisible (smpName);
            ratioLabel.setText (u8 ("FM pomer"), juce::dontSendNotification); ratioLabel.setFont (juce::FontOptions (12.5f));
            ratioLabel.setColour (juce::Label::textColourId, dim.brighter (0.2f)); ratioLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (ratioLabel);
            src.setTooltip (u8 ("Klavír = vstavané nahrávky klavíra, Vlastný WAV = tvoj zvuk"));
            update();
        }
        std::unique_ptr<Knob> mk (const char* id, const juce::String& title)
        {
            auto k = std::make_unique<Knob> (p.params, pre + id, title);
            if (li == 1) k->setArc (arcB);
            addAndMakeVisible (*k);
            return k;
        }
        void chooseFile()
        {
            static juce::File lastDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            chooser = std::make_unique<juce::FileChooser> (u8 ("Zvuk do vrstvy"), lastDir, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            juce::Component::SafePointer<LayerPanel> sp (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (sp == nullptr || ! f.existsAsFile()) return;
                lastDir = f.getParentDirectory();
                juce::String err;
                if (! sp->p.loadSampleFile (sp->li, f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Nova", err);
                sp->update();
            });
        }
        void update()
        {
            const int w = (int) p.params.getRawParameterValue (pre + "wave")->load();
            const bool isSample = w == 7, isUser = isSample && p.params.getRawParameterValue (pre + "src")->load() > 0.5f;
            if (w != lastWave || isUser != lastUser)
            {
                lastWave = w; lastUser = isUser;
                const char* ct = "Farba";
                switch (w) { case 3: ct = "Šírka pulzu"; break; case 4: ct = "Farba šumu"; break; case 5: ct = "Jas struny"; break; case 6: ct = "FM index"; break; case 7: ct = "Tvrdosť"; break; default: break; }
                color->setTitle (u8 (ct));
                color->setVisible (w >= 3 && ! isUser);
                ratio.setVisible (w == 6); ratioLabel.setVisible (w == 6);
                src.setVisible (isSample); load.setVisible (isSample); root->setVisible (isUser); loop.setVisible (isUser); smpName.setVisible (isUser);
                row1[3]->setEnabled (w <= 3); row1[4]->setEnabled (w <= 3);
                resized();
            }
            const auto nm = p.sampleName (li);
            smpName.setText (nm.isNotEmpty() ? nm : u8 ("(žiadny zvuk)"), juce::dontSendNotification);
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            sectionLabel (g, { 12, 118, 50, 20 }, "FILTER");
            sectionLabel (g, { 12 + 3 * 60, 118, 130, 20 }, u8 ("OBÁLKA FILTRA"));
            sectionLabel (g, { 12, 222, 160, 20 }, u8 ("OBÁLKA HLASITOSTI"));
            sectionLabel (g, { 12 + 4 * 60, 222, 30, 20 }, "LFO");
            g.setColour (line); g.fillRect (12 + 3 * 60 - 2, 146, 1, 66); g.fillRect (12 + 4 * 60 - 2, 250, 1, 66);
            g.fillRect (12, 326, getWidth() - 24, 1);
        }
        void resized() override
        {
            on.setBounds (10, 8, 110, 24);
            wave.setBounds (124, 9, getWidth() - 134, 22);
            for (int i = 0; i < 7; ++i)
            {
                row1[(size_t) i]->setBounds (12 + i * 60, 40, 58, 74);
                row2[(size_t) i]->setBounds (12 + i * 60, 144, 58, 74);
                row3[(size_t) i]->setBounds (12 + i * 60, 248, 58, 74);
            }
            ftype.setBounds (62, 117, 104, 21);
            f24.setBounds (getWidth() - 76, 117, 66, 22);
            lshape.setBounds (12 + 4 * 60 + 32, 220, 72, 22);
            ltgt.setBounds (12 + 4 * 60 + 108, 220, getWidth() - (12 + 4 * 60 + 108) - 12, 22);
            int x = 12;
            color->setBounds (x, 332, 70, 72); if (color->isVisible()) x += 78;
            ratioLabel.setBounds (x, 334, 84, 15); ratio.setBounds (x, 354, 84, 24); if (ratio.isVisible()) x += 94;
            src.setBounds (x, 336, 124, 24);
            load.setBounds (x, 366, 124, 26);
            if (src.isVisible()) x += 134;
            root->setBounds (getWidth() - 12 - 64 - 92, 332, 64, 72);
            loop.setBounds (getWidth() - 12 - 88, 366, 88, 22);
            smpName.setBounds (x, 336, juce::jmax (40, root->getX() - x - 6), 22);
        }
        NovaProcessor& p; int li; juce::String pre;
        Toggle on; Seg wave; Combo ftype; Toggle f24; Combo lshape, ltgt, ratio, src; Toggle loop;
        std::vector<std::unique_ptr<Knob>> row1, row2, row3; std::unique_ptr<Knob> color, root;
        juce::TextButton load; juce::Label smpName, ratioLabel;
        std::unique_ptr<juce::FileChooser> chooser;
        int lastWave = -1; bool lastUser = false;
    };

    // ===================================================================== tube
    struct TubePanel : juce::Component
    {
        TubePanel (NovaProcessor& pr) : p (pr), analog (pr.params, "tube", u8 ("Analóg")), comp (pr.params, "comp", u8 ("Kompresia")), compOn (pr.params, "comp_on", u8 ("Tube komp"), amber.brighter (0.3f))
        {
            addAndMakeVisible (analog); addAndMakeVisible (comp); addAndMakeVisible (compOn);
            analog.setArc (amber); comp.setArc (amber);
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds(), "TUBE", amber.brighter (0.3f));
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText (u8 ("Teplo a jemné\n„plávanie“ ladenia"), juce::Rectangle<int> (10, 140, getWidth() - 20, 32), juce::Justification::centred, 2);
            g.drawFittedText (u8 ("Vyrovná hlasité tóny"), juce::Rectangle<int> (10, 326, getWidth() - 20, 18), juce::Justification::centred, 1);
            auto bar = juce::Rectangle<float> (14.0f, 360.0f, (float) getWidth() - 28.0f, 8.0f);
            g.setColour (steel); g.fillRoundedRectangle (bar, 3.0f);
            const float gr = juce::jlimit (0.0f, 20.0f, p.synth.gainReduction());
            g.setColour (amber); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * gr / 20.0f), 3.0f);
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText (u8 ("Stíšenie ") + juce::String (gr, 1) + " dB", juce::Rectangle<int> (10, 372, getWidth() - 20, 18), juce::Justification::centred);
        }
        void resized() override
        {
            analog.setBounds (20, 36, getWidth() - 40, 100);
            compOn.setBounds (14, 190, getWidth() - 20, 24);
            comp.setBounds (25, 220, getWidth() - 50, 100);
        }
        NovaProcessor& p; Knob analog, comp; Toggle compOn;
    };

    // ===================================================================== efekty + EQ
    struct FxPanel : juce::Component
    {
        FxPanel (NovaProcessor& pr) : p (pr),
            drive (pr.params, "drive_on"), chorus (pr.params, "ch_on"), delay (pr.params, "dl_on"), reverb (pr.params, "rv_on"), pp (pr.params, "dl_pp"),
            sync (pr.params, "dl_sync")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &drive, &chorus, &delay, &reverb, &pp, &sync }) addAndMakeVisible (c);
            auto add = [this] (std::vector<std::unique_ptr<Knob>>& v, const char* id, const char* t) { v.push_back (std::make_unique<Knob> (p.params, id, u8 (t))); addAndMakeVisible (*v.back()); };
            add (fx, "drive", "Sila");
            add (fx, "ch_rate", "Rýchlosť"); add (fx, "ch_depth", "Hĺbka"); add (fx, "ch_mix", "Mix");
            add (fx, "dl_time", "Čas"); add (fx, "dl_fb", "Opakov."); add (fx, "dl_tone", "Farba"); add (fx, "dl_mix", "Mix");
            add (fx, "rv_size", "Veľkosť"); add (fx, "rv_damp", "Tlmenie"); add (fx, "rv_mix", "Mix");
            add (eq, "eq_low", "Basy"); add (eq, "eq_mid", "Stredy"); add (eq, "eq_midf", "Stredy Hz"); add (eq, "eq_high", "Výšky");
            for (auto* b : { &tabFx, &tabEq }) addAndMakeVisible (b);
            tabFx.setButtonText (u8 ("EFEKTY")); tabEq.setButtonText ("EQ");
            tabFx.onClick = [this] { showEq (false); }; tabEq.onClick = [this] { showEq (true); };
            auto mkSlider = [this] (juce::Slider& s, juce::Label& l, const char* id, const char* t)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                addAndMakeVisible (s); atts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params, id, s));
                l.setText (u8 (t), juce::dontSendNotification); l.setFont (juce::FontOptions (12.5f)); l.setColour (juce::Label::textColourId, dim.brighter (0.2f));
                addAndMakeVisible (l);
                s.setDoubleClickReturnValue (true, findSpec (id)->def);
            };
            mkSlider (width, widthL, "fx_width", "Šírka"); mkSlider (punch, punchL, "fx_punch", "Úder");
            showEq (false);
        }
        void showEq (bool e)
        {
            eqShown = e;
            tabFx.setToggleState (! e, juce::dontSendNotification); tabEq.setToggleState (e, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &drive, &chorus, &delay, &reverb, &pp, &sync }) c->setVisible (! e);
            for (auto& k : fx) k->setVisible (! e);
            for (auto& k : eq) k->setVisible (e);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            if (eqShown) { g.setColour (dim); g.setFont (juce::FontOptions (12.0f)); g.drawText (u8 ("Ekvalizér celého zvuku"), 24, 180, 300, 20, juce::Justification::centredLeft); }
            else { g.setColour (line); g.fillRect (276, 44, 1, 196); }
        }
        void resized() override
        {
            tabFx.setBounds (10, 8, 72, 22); tabEq.setBounds (86, 8, 40, 22);
            widthL.setBounds (150, 8, 44, 22); width.setBounds (194, 8, 130, 22);
            punchL.setBounds (338, 8, 40, 22); punch.setBounds (378, 8, getWidth() - 390, 22);
            drive.setBounds (12, 40, 100, 22);
            fx[0]->setBounds (12, 62, 58, 72);
            delay.setBounds (12, 142, 74, 22); sync.setBounds (88, 142, 92, 22); pp.setBounds (184, 142, 90, 22);
            for (int i = 0; i < 4; ++i) fx[(size_t) (4 + i)]->setBounds (12 + i * 64, 166, 60, 72);
            const int x2 = 290;
            chorus.setBounds (x2, 40, 100, 22);
            for (int i = 0; i < 3; ++i) fx[(size_t) (1 + i)]->setBounds (x2 + i * 64, 62, 60, 72);
            reverb.setBounds (x2, 142, 100, 22);
            for (int i = 0; i < 3; ++i) fx[(size_t) (8 + i)]->setBounds (x2 + i * 64, 166, 60, 72);
            for (int i = 0; i < 4; ++i) eq[(size_t) i]->setBounds (24 + i * 100, 56, 80, 100);
        }
        NovaProcessor& p;
        Toggle drive, chorus, delay, reverb, pp; Combo sync;
        std::vector<std::unique_ptr<Knob>> fx, eq;
        juce::TextButton tabFx, tabEq; juce::Slider width, punch; juce::Label widthL, punchL;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
        bool eqShown = false;
    };

    // ===================================================================== arpeggiator + hlas
    struct ArpPanel : juce::Component
    {
        ArpPanel (NovaProcessor& pr) : on (pr.params, "arp_on", u8 ("ARPEGGIÁTOR"), amber.brighter (0.3f)), hold (pr.params, "arp_hold", u8 ("Držať"), amber.brighter (0.3f)),
            mono (pr.params, "mono", "Mono"),
            pat (pr.params, "arp_pat", juce::StringArray::fromTokens (u8 ("Hore|Dole|H/D|Hrám|Náhod.|Akord"), "|", ""), amber),
            speed (pr.params, "arp_speed", juce::StringArray::fromTokens ("1/4|1/8|1/8T|1/16|1/32", "|", ""), amber),
            oct (pr.params, "arp_oct", u8 ("Oktávy")), len (pr.params, "arp_len", u8 ("Dĺžka")), glide (pr.params, "glide"), vel (pr.params, "veltrack", u8 ("Citlivosť"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &hold, &mono, &pat, &speed, &oct, &len, &glide, &vel }) addAndMakeVisible (c);
            oct.setArc (amber); len.setArc (amber);
            vel.s.setTooltip (u8 ("Ako veľmi hlasitosť závisí od sily úderu"));
            mono.setTooltip (u8 ("Hrá vždy len jeden tón (basy, leady) – s Glide sa tóny prelínajú"));
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            sectionLabel (g, { 12, 38, 100, 18 }, "VZOR");
            sectionLabel (g, { 12, 88, 100, 18 }, u8 ("RÝCHLOSŤ"));
            g.setColour (line); g.fillRect (12 + 2 * ((getWidth() - 24) / 4) - 2, 144, 1, 110);
        }
        void resized() override
        {
            on.setBounds (10, 8, 150, 24); hold.setBounds (getWidth() - 86, 8, 80, 24);
            pat.setBounds (12, 58, getWidth() - 24, 24);
            speed.setBounds (12, 108, getWidth() - 24, 24);
            const int w = (getWidth() - 24) / 4;
            oct.setBounds (12, 142, w, 74); len.setBounds (12 + w, 142, w, 74); glide.setBounds (12 + 2 * w, 142, w, 74); vel.setBounds (12 + 3 * w, 142, w, 74);
            mono.setBounds (12 + 2 * w + 4, 228, 100, 24);
        }
        Toggle on, hold, mono; Seg pat, speed; Knob oct, len, glide, vel;
    };

    // ===================================================================== morph XY
    struct MorphPad : juce::Component, juce::SettableTooltipClient
    {
        MorphPad (NovaProcessor& pr) : p (pr)
        {
            ax = std::make_unique<juce::ParameterAttachment> (*pr.params.getParameter ("morph_x"), [this] (float v) { x = v; repaint(); }, &pr.undo);
            ay = std::make_unique<juce::ParameterAttachment> (*pr.params.getParameter ("morph_y"), [this] (float v) { y = v; repaint(); }, &pr.undo);
            ax->sendInitialUpdate(); ay->sendInitialUpdate();
            setTooltip (u8 ("Vľavo vrstva A, vpravo vrstva B · hore jasnejší, dole tmavší · dvojklik = stred"));
        }
        juce::Rectangle<float> pad() const { const float s = juce::jmin ((float) getWidth() - 44.0f, (float) getHeight() - 64.0f); return { ((float) getWidth() - s) * 0.5f, 42.0f, s, s }; }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds(), "MORPH");
            g.setColour (dim); g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (p.synth.activeVoices()) + u8 (" hlasov"), getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::centredRight);
            const auto r = pad();
            for (int i = 4; i >= 1; --i) { g.setColour (steel.withAlpha (0.25f + 0.12f * (float) (4 - i))); const float k = (float) i / 4.0f; g.fillEllipse (r.withSizeKeepingCentre (r.getWidth() * k, r.getHeight() * k)); }
            g.setColour (line.brighter (0.2f)); g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY()); g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom());
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText ("A", juce::Rectangle<float> (r.getX() - 16, r.getCentreY() - 8, 14, 16), juce::Justification::centred);
            g.drawText ("B", juce::Rectangle<float> (r.getRight() + 2, r.getCentreY() - 8, 14, 16), juce::Justification::centred);
            g.drawText (u8 ("jasný"), juce::Rectangle<float> (r.getCentreX() - 30, r.getY() - 15, 60, 14), juce::Justification::centred);
            g.drawText (u8 ("tmavý"), juce::Rectangle<float> (r.getCentreX() - 30, r.getBottom() + 1, 60, 14), juce::Justification::centred);
            const float px = r.getCentreX() + x * r.getWidth() * 0.5f, py = r.getCentreY() - y * r.getHeight() * 0.5f;
            g.setColour (text); g.fillEllipse (px - 8, py - 8, 16, 16);
            g.setColour (arc); g.drawEllipse (px - 11, py - 11, 22, 22, 1.5f);
        }
        void setFrom (juce::Point<float> pt)
        {
            const auto r = pad();
            const float nx = juce::jlimit (-1.0f, 1.0f, (pt.x - r.getCentreX()) / (r.getWidth() * 0.5f));
            const float ny = juce::jlimit (-1.0f, 1.0f, -(pt.y - r.getCentreY()) / (r.getHeight() * 0.5f));
            ax->setValueAsPartOfGesture (nx); ay->setValueAsPartOfGesture (ny);
        }
        void mouseDown (const juce::MouseEvent& e) override { ax->beginGesture(); ay->beginGesture(); setFrom (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { setFrom (e.position); }
        void mouseUp (const juce::MouseEvent&) override { ax->endGesture(); ay->endGesture(); }
        void mouseDoubleClick (const juce::MouseEvent&) override { ax->setValueAsCompleteGesture (0); ay->setValueAsCompleteGesture (0); }
        NovaProcessor& p; float x = 0, y = 0;
        std::unique_ptr<juce::ParameterAttachment> ax, ay;
    };

    // ===================================================================== master
    struct MasterPanel : juce::Component
    {
        MasterPanel (NovaProcessor& pr) : on (pr.params, "mf_on", "MASTER FILTER", amber.brighter (0.3f)),
            type (pr.params, "mf_type", juce::StringArray::fromTokens (u8 ("Dolná|Horná"), "|", ""), amber),
            cut (pr.params, "mf_cut"), reso (pr.params, "mf_reso", u8 ("Rezon.")), drive (pr.params, "mf_drive"), gain (pr.params, "p_gain", u8 ("Úroveň"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &type, &cut, &reso, &drive, &gain }) addAndMakeVisible (c);
            cut.setArc (amber); reso.setArc (amber); drive.setArc (amber);
            gain.s.setTooltip (u8 ("Hlasitosť tohto zvuku (ukladá sa so zvukom)"));
        }
        void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds()); g.setColour (line); g.fillRect (12 + 3 * ((getWidth() - 24) / 4) - 2, 36, 1, 64); }
        void resized() override
        {
            on.setBounds (10, 6, 150, 22); type.setBounds (getWidth() - 132, 6, 120, 22);
            const int w = (getWidth() - 24) / 4;
            cut.setBounds (12, 32, w, 72); reso.setBounds (12 + w, 32, w, 72); drive.setBounds (12 + 2 * w, 32, w, 72); gain.setBounds (12 + 3 * w, 32, w, 72);
        }
        Toggle on; Seg type; Knob cut, reso, drive, gain;
    };

    // ===================================================================== zoznam zvukov
    struct Browser;
    struct CategoryChips : juce::Component, juce::DragAndDropTarget
    {
        CategoryChips (Browser& b) : br (b) {}
        juce::StringArray cats() const { juce::StringArray c; c.add (u8 ("Všetko")); c.addArray (categories()); return c; }
        std::vector<juce::Rectangle<int>> layoutChips (int width) const
        {
            std::vector<juce::Rectangle<int>> r; int x = 0, y = 0;
            const juce::Font f (juce::FontOptions (13.0f));
            for (auto& c : cats())
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (f, c) + 14;
                if (x + w > width && x > 0) { x = 0; y += 26; }
                r.push_back ({ x, y, w, 22 }); x += w + 5;
            }
            return r;
        }
        int heightFor (int width) const { auto r = layoutChips (width); return r.empty() ? 0 : r.back().getBottom(); }
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override;
        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("preset:"); }
        void itemDragMove (const SourceDetails& d) override { hoverDrop = chipAt (d.localPosition.toInt()); repaint(); }
        void itemDragExit (const SourceDetails&) override { hoverDrop = -1; repaint(); }
        void itemDropped (const SourceDetails& d) override;
        int chipAt (juce::Point<int> p) const { auto r = layoutChips (getWidth()); for (int i = 0; i < (int) r.size(); ++i) if (r[(size_t) i].contains (p)) return i; return -1; }
        Browser& br; int hoverDrop = -1;
    };

    struct PresetList : juce::Component, juce::DragAndDropTarget
    {
        PresetList (Browser& b) : br (b) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("preset:"); }
        void itemDragMove (const SourceDetails& d) override { dropRow = rowAt (d.localPosition.y + rowH / 2); repaint(); }
        void itemDragExit (const SourceDetails&) override { dropRow = -1; repaint(); }
        void itemDropped (const SourceDetails& d) override;
        int rowAt (int y) const { return juce::jmax (0, y / rowH); }
        static constexpr int rowH = 25;
        Browser& br; int dropRow = -1, downRow = -1; bool dragging = false;
    };

    struct Browser : juce::Component, juce::TextEditor::Listener
    {
        Browser (NovaProcessor& pr) : p (pr), chips (*this), list (*this)
        {
            search.setTextToShowWhenEmpty (u8 ("Hľadať zvuk…"), dim);
            search.addListener (this);
            search.setFont (juce::FontOptions (13.5f));
            addAndMakeVisible (search);
            importBtn.setButtonText (u8 ("Import zvuku…"));
            importBtn.setTooltip (u8 ("Pridať zvuk zo súboru .knova (napr. z iného počítača)"));
            importBtn.onClick = [this] { importDialog(); };
            folderBtn.setButtonText (u8 ("Priečinok"));
            folderBtn.setTooltip (u8 ("Otvoriť priečinok s vlastnými zvukmi"));
            folderBtn.onClick = [this] { p.presets.folder().createDirectory(); p.presets.folder().startAsProcess(); };
            addAndMakeVisible (importBtn); addAndMakeVisible (folderBtn);
            addAndMakeVisible (chips);
            vp.setViewedComponent (&list, false); vp.setScrollBarsShown (true, false); vp.setScrollBarThickness (8);
            addAndMakeVisible (vp);
            hint.setText (u8 ("Ťahaním myšou meníš poradie, pustením na kategóriu ju zmeníš. Pravé tlačidlo = ďalšie možnosti."), juce::dontSendNotification);
            hint.setFont (juce::FontOptions (11.5f)); hint.setColour (juce::Label::textColourId, dim); hint.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (hint);
            refresh();
        }
        static juce::String& filterCat() { static juce::String c; return c; }
        void textEditorTextChanged (juce::TextEditor&) override { refresh(); }
        void refresh()
        {
            p.presets.refresh();
            shown.clear();
            const auto q = search.getText().trim().toLowerCase();
            for (auto& e : p.presets.entries())
            {
                if (filterCat().isNotEmpty() && e.category != filterCat()) continue;
                if (q.isNotEmpty() && ! e.name.toLowerCase().contains (q) && ! e.category.toLowerCase().contains (q)) continue;
                shown.push_back (e);
            }
            list.setSize (juce::jmax (10, vp.getMaximumVisibleWidth()), (int) shown.size() * PresetList::rowH + 4);
            list.repaint(); chips.repaint();
        }
        void step (int d)
        {
            if (shown.empty()) return;
            int i = d > 0 ? 0 : (int) shown.size() - 1;
            for (int k = 0; k < (int) shown.size(); ++k) if (shown[(size_t) k].key == p.currentKey) i = k + d;
            i = (i + (int) shown.size()) % (int) shown.size();
            p.loadPreset (shown[(size_t) i].key);
            list.repaint();
        }
        void importDialog()
        {
            chooser = std::make_unique<juce::FileChooser> (u8 ("Import zvuku"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.knova");
            juce::Component::SafePointer<Browser> sp (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [sp] (const juce::FileChooser& fc)
            {
                if (sp == nullptr) return;
                juce::String last;
                for (auto& f : fc.getResults()) { auto k = sp->p.presets.importFile (f); if (k.isNotEmpty()) last = k; }
                if (last.isNotEmpty()) { sp->p.loadPreset (last); sp->refresh(); }
            });
        }
        void itemMenu (const PresetStore::Entry& e)
        {
            juce::PopupMenu m;
            m.addItem (1, u8 ("Načítať"));
            m.addItem (2, u8 ("Premenovať…"), ! e.factory);
            m.addItem (3, u8 ("Duplikovať"));
            m.addItem (4, u8 ("Exportovať…"));
            juce::PopupMenu cats;
            for (int i = 0; i < categories().size(); ++i) cats.addItem (100 + i, categories()[i], true, e.category == categories()[i]);
            m.addSubMenu (u8 ("Kategória"), cats);
            m.addSeparator();
            m.addItem (5, u8 ("Zmazať"), ! e.factory);
            const auto key = e.key; const auto name = e.name;
            juce::Component::SafePointer<Browser> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [sp, key, name] (int r)
            {
                if (sp == nullptr || r == 0) return;
                auto& b = *sp;
                if (r == 1) b.p.loadPreset (key);
                else if (r == 2) b.renameDialog (key, name);
                else if (r == 3) { auto k = b.p.presets.duplicate (key); if (k.isNotEmpty()) b.p.loadPreset (k); }
                else if (r == 4)
                {
                    b.chooser = std::make_unique<juce::FileChooser> (u8 ("Exportovať zvuk"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (name) + ".knova"), "*.knova");
                    b.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [sp, key] (const juce::FileChooser& fc)
                    { if (sp != nullptr && fc.getResult() != juce::File()) sp->p.presets.exportTo (key, fc.getResult().withFileExtension ("knova")); });
                }
                else if (r == 5)
                {
                    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, u8 ("Zmazať zvuk"), u8 ("Naozaj zmazať zvuk „") + name + u8 ("“?"), u8 ("Zmazať"), u8 ("Zrušiť"), nullptr,
                        juce::ModalCallbackFunction::create ([sp, key] (int ok) { if (ok && sp != nullptr) { sp->p.presets.remove (key); sp->refresh(); } }));
                }
                else if (r >= 100) b.p.presets.setCategory (key, categories()[r - 100]);
                b.refresh();
            });
        }
        void renameDialog (const juce::String& key, const juce::String& name)
        {
            auto* w = new juce::AlertWindow (u8 ("Premenovať zvuk"), {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", name, u8 ("Nový názov"));
            w->addButton (u8 ("Premenovať"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (u8 ("Zrušiť"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<Browser> sp (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w, key] (int r)
            {
                if (r == 1 && sp != nullptr)
                {
                    const auto nn = w->getTextEditorContents ("name").trim();
                    const bool wasCurrent = sp->p.currentKey == key;
                    if (sp->p.presets.rename (key, nn) && wasCurrent) { sp->p.currentKey = "u:" + juce::File::createLegalFileName (nn) + ".knova"; sp->p.currentName = nn; }
                    sp->refresh();
                }
            }), true);
        }
        void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), "ZVUKY"); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 0);
            r.removeFromTop (32);
            search.setBounds (r.removeFromTop (26)); r.removeFromTop (6);
            auto br = r.removeFromTop (24);
            importBtn.setBounds (br.removeFromLeft (br.getWidth() / 2 + 20).withTrimmedRight (3)); folderBtn.setBounds (br.withTrimmedLeft (3));
            r.removeFromTop (8);
            const int ch = chips.heightFor (r.getWidth());
            chips.setBounds (r.removeFromTop (ch)); r.removeFromTop (8);
            hint.setBounds (r.removeFromBottom (40).withTrimmedBottom (6));
            vp.setBounds (r.withTrimmedBottom (4));
            refresh();
        }
        NovaProcessor& p;
        juce::TextEditor search; juce::TextButton importBtn, folderBtn;
        CategoryChips chips; PresetList list; juce::Viewport vp; juce::Label hint;
        std::vector<PresetStore::Entry> shown;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    void CategoryChips::paint (juce::Graphics& g)
    {
        auto r = layoutChips (getWidth()); auto c = cats();
        for (int i = 0; i < (int) r.size(); ++i)
        {
            const bool sel = (i == 0 && Browser::filterCat().isEmpty()) || (i > 0 && Browser::filterCat() == c[i]);
            g.setColour (i == hoverDrop && i > 0 ? amber : (sel ? steelHi : steel));
            g.fillRoundedRectangle (r[(size_t) i].toFloat(), 3.0f);
            g.setColour (sel ? juce::Colours::white : text.withAlpha (0.85f)); g.setFont (juce::FontOptions (13.0f));
            g.drawText (c[i], r[(size_t) i], juce::Justification::centred);
        }
    }
    void CategoryChips::mouseUp (const juce::MouseEvent& e)
    {
        const int i = chipAt (e.getPosition());
        if (i < 0) return;
        Browser::filterCat() = i == 0 ? juce::String() : cats()[i];
        br.refresh();
    }
    void CategoryChips::itemDropped (const SourceDetails& d)
    {
        const int i = chipAt (d.localPosition.toInt()); hoverDrop = -1;
        if (i > 0) { br.p.presets.setCategory (d.description.toString().fromFirstOccurrenceOf ("preset:", false, false), cats()[i]); br.refresh(); }
        repaint();
    }

    void PresetList::paint (juce::Graphics& g)
    {
        for (int i = 0; i < (int) br.shown.size(); ++i)
        {
            const auto& e = br.shown[(size_t) i];
            auto r = juce::Rectangle<int> (0, i * rowH, getWidth(), rowH - 2);
            const bool sel = e.key == br.p.currentKey;
            if (sel) { g.setColour (steelHi); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
            g.setColour (sel ? juce::Colours::white : text); g.setFont (juce::FontOptions (14.0f));
            g.drawText (e.name, r.reduced (8, 0), juce::Justification::centredLeft);
            g.setColour (sel ? text : dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText (e.category, r.reduced (8, 0), juce::Justification::centredRight);
        }
        if (dropRow >= 0) { g.setColour (amber); g.fillRect (0, juce::jmin (dropRow, (int) br.shown.size()) * rowH - 2, getWidth(), 3); }
    }
    void PresetList::mouseDown (const juce::MouseEvent& e)
    {
        downRow = e.y / rowH; dragging = false;
        if (! juce::isPositiveAndBelow (downRow, (int) br.shown.size())) return;
        if (e.mods.isPopupMenu()) br.itemMenu (br.shown[(size_t) downRow]);
    }
    void PresetList::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || e.mods.isPopupMenu() || ! juce::isPositiveAndBelow (downRow, (int) br.shown.size()) || e.getDistanceFromDragStart() < 6) return;
        dragging = true;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            // obrazok len tazeneho riadku
            const auto& en = br.shown[(size_t) downRow];
            juce::Image img (juce::Image::ARGB, getWidth(), rowH - 2, true);
            {
                juce::Graphics g (img);
                g.setColour (steelHi.withAlpha (0.9f)); g.fillRoundedRectangle (img.getBounds().toFloat(), 3.0f);
                g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (14.0f));
                g.drawText (en.name, img.getBounds().reduced (8, 0), juce::Justification::centredLeft);
            }
            c->startDragging ("preset:" + en.key, this, juce::ScaledImage (img), false, nullptr, &e.source);
        }
    }
    void PresetList::mouseUp (const juce::MouseEvent& e)
    {
        if (dragging || e.mods.isPopupMenu()) { dragging = false; return; }
        const int r = e.y / rowH;
        if (r == downRow && juce::isPositiveAndBelow (r, (int) br.shown.size())) { br.p.loadPreset (br.shown[(size_t) r].key); repaint(); }
    }
    void PresetList::itemDropped (const SourceDetails& d)
    {
        const auto key = d.description.toString().fromFirstOccurrenceOf ("preset:", false, false);
        const int row = juce::jlimit (0, (int) br.shown.size(), rowAt (d.localPosition.y + rowH / 2));
        dropRow = -1;
        br.p.presets.move (key, row < (int) br.shown.size() ? br.shown[(size_t) row].key : juce::String());
        br.refresh();
    }

    // ===================================================================== horna lista rozsirenej verzie
    struct TopBar : juce::Component
    {
        TopBar (NovaEditor& e, Browser& b) : ed (e), p (e.proc), br (b)
        {
            simple.setButtonText (u8 ("Jednoduchá")); advanced.setButtonText (u8 ("Rozšírená"));
            advanced.setToggleState (true, juce::dontSendNotification);
            simple.onClick = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (0); }); };
            prev.setButtonText (juce::String::charToString (0x25C0)); next.setButtonText (juce::String::charToString (0x25B6));
            prev.onClick = [this] { br.step (-1); }; next.onClick = [this] { br.step (1); };
            save.setButtonText (u8 ("Uložiť…")); save.onClick = [this] { saveDialog(); };
            undoB.setButtonText (u8 ("Späť")); redoB.setButtonText (u8 ("Znova"));
            undoB.onClick = [this] { p.undo.undo(); }; redoB.onClick = [this] { p.undo.redo(); };
            for (int s : { 75, 90, 100, 110, 125, 150 }) size.addItem (juce::String (s) + " %", s);
            size.setSelectedId (juce::roundToInt (p.uiScale * 100.0f), juce::dontSendNotification);
            if (size.getSelectedId() == 0) size.setSelectedId (100, juce::dontSendNotification);
            size.onChange = [this] { const float s = (float) size.getSelectedId() / 100.0f; juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed), s] { if (sp != nullptr) sp->setScale (s); }); };
            vol.setSliderStyle (juce::Slider::LinearHorizontal); vol.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params, "volume", vol);
            vol.setDoubleClickReturnValue (true, 0.0);
            for (auto* c : std::initializer_list<juce::Component*> { &simple, &advanced, &prev, &next, &save, &undoB, &redoB, &size, &vol }) addAndMakeVisible (c);
            tick();
        }
        void saveDialog()
        {
            auto* w = new juce::AlertWindow (u8 ("Uložiť zvuk"), u8 ("Zvuk sa uloží medzi vlastné zvuky."), juce::MessageBoxIconType::NoIcon);
            const bool factory = p.currentKey.startsWith ("f:");
            w->addTextEditor ("name", factory ? p.currentName + u8 (" (môj)") : p.currentName, u8 ("Názov"));
            w->addComboBox ("cat", categories(), u8 ("Kategória"));
            if (auto* cb = w->getComboBoxComponent ("cat"))
            {
                const int ci = categories().indexOf (p.currentCategory);
                cb->setSelectedItemIndex (ci >= 0 ? ci : categories().size() - 1);
            }
            w->addButton (u8 ("Uložiť"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (u8 ("Zrušiť"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<TopBar> sp (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r)
            {
                if (r != 1 || sp == nullptr) return;
                const auto name = w->getTextEditorContents ("name").trim();
                const auto cat = w->getComboBoxComponent ("cat")->getText();
                if (name.isEmpty()) return;
                sp->p.savePreset (name, cat);
                sp->br.refresh();
            }), true);
        }
        void tick()
        {
            undoB.setEnabled (p.undo.canUndo()); redoB.setEnabled (p.undo.canRedo());
            if (p.currentName != shownName || p.currentCategory != shownCat) { shownName = p.currentName; shownCat = p.currentCategory; repaint(); }
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (panel); g.fillRect (getLocalBounds());
            g.setColour (juce::Colour (0xff1a2029)); g.fillRoundedRectangle (nameBox.toFloat(), 3.0f);
            g.setColour (text); g.setFont (juce::FontOptions (16.0f));
            g.drawText (shownName, nameBox.reduced (12, 0), juce::Justification::centredLeft);
            g.setColour (dim); g.setFont (juce::FontOptions (12.5f));
            g.drawText (shownCat, nameBox.reduced (12, 0), juce::Justification::centredRight);
            g.drawText (u8 ("Veľkosť"), size.getBounds().translated (-62, 0).withWidth (56), juce::Justification::centredRight);
            g.drawText (u8 ("Hlasitosť"), vol.getBounds().translated (-72, 0).withWidth (66), juce::Justification::centredRight);
        }
        void resized() override
        {
            const int y = 12, h = 28;
            simple.setBounds (12, y, 96, h); advanced.setBounds (110, y, 96, h);
            prev.setBounds (226, y, 32, h);
            nameBox = { 262, y, 330, h };
            next.setBounds (596, y, 32, h);
            save.setBounds (640, y, 84, h); undoB.setBounds (730, y, 64, h); redoB.setBounds (798, y, 64, h);
            vol.setBounds (getWidth() - 12 - 150, y, 150, h);
            size.setBounds (vol.getX() - 76 - 96, y + 2, 90, h - 4);
        }
        NovaEditor& ed; NovaProcessor& p; Browser& br;
        juce::TextButton simple, advanced, prev, next, save, undoB, redoB;
        juce::ComboBox size; juce::Slider vol;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAtt;
        juce::Rectangle<int> nameBox; juce::String shownName, shownCat;
    };

    // ===================================================================== rozsirena verzia
    struct ExtendedView : juce::Component, Tickable
    {
        ExtendedView (NovaEditor& e) : ed (e), p (e.proc), browser (p), top (e, browser), la (p, 0), lb (p, 1), tube (p), fx (p), arp (p), morph (p), master (p),
            keys (p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &top, &browser, &la, &lb, &tube, &fx, &arp, &morph, &master, &keys }) addAndMakeVisible (c);
            keys.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd9dee4));
            keys.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1c232c));
            keys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff8b98a8));
            keys.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, steelHi.withAlpha (0.8f));
            keys.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, arc.withAlpha (0.3f));
            keys.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
            keys.setKeyWidth (19.0f); keys.setAvailableRange (24, 108); keys.setLowestVisibleKey (36);
            keys.setWantsKeyboardFocus (false);
            setSize (EW, EH);
        }
        void tick() override
        {
            top.tick(); la.update(); lb.update(); tube.repaint(); morph.repaint();
            if (p.currentKey != lastKey) { lastKey = p.currentKey; browser.list.repaint(); }
        }
        int layerAt (juce::Point<int> pt) const { return lb.getBounds().contains (pt) ? 1 : 0; }
        void paint (juce::Graphics& g) override { g.fillAll (bg); }
        void resized() override
        {
            top.setBounds (0, 0, getWidth(), 52);
            const int x0 = 12, y0 = 52 + 10;
            browser.setBounds (x0, y0, 240, getHeight() - y0 - 12);
            const int mx = x0 + 240 + 12;
            la.setBounds (mx, y0, 440, 410);
            tube.setBounds (mx + 448, y0, 150, 410);
            lb.setBounds (mx + 448 + 158, y0, 440, 410);
            const int y1 = y0 + 410 + 8;
            fx.setBounds (mx, y1, 530, 270);
            arp.setBounds (mx + 538, y1, 280, 270);
            morph.setBounds (mx + 538 + 288, y1, 220, 270);
            const int y2 = y1 + 270 + 8;
            master.setBounds (mx, y2, 300, getHeight() - y2 - 12);
            keys.setBounds (mx + 308, y2, getWidth() - (mx + 308) - 12, getHeight() - y2 - 12);
        }
        NovaEditor& ed; NovaProcessor& p;
        Browser browser; TopBar top; LayerPanel la, lb; TubePanel tube; FxPanel fx; ArpPanel arp; MorphPad morph; MasterPanel master;
        juce::MidiKeyboardComponent keys;
        juce::String lastKey;
    };
}

// ===================================================================== editor
NovaEditor::NovaEditor (NovaProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    setMode (proc.uiMode);
    proc.onPatchChanged = [sp = juce::Component::SafePointer<NovaEditor> (this)]
    {
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->timerCallback(); });
    };
    startTimerHz (15);
}

NovaEditor::~NovaEditor()
{
    proc.onPatchChanged = nullptr;
    view = nullptr;
    setLookAndFeel (nullptr);
}

void NovaEditor::setMode (int mode)
{
    proc.uiMode = mode;
    content.removeAllChildren();
    view = nullptr;
    if (mode == 1) view = std::make_unique<ExtendedView> (*this);
    else view = std::make_unique<SimpleView> (*this);
    content.addAndMakeVisible (*view);
    layoutSize();
}

void NovaEditor::setScale (float s)
{
    proc.uiScale = juce::jlimit (0.5f, 2.0f, s);
    layoutSize();
}

void NovaEditor::layoutSize()
{
    if (view == nullptr) return;
    const float s = proc.uiMode == 1 ? proc.uiScale : 1.0f;
    view->setTopLeftPosition (0, 0);
    content.setTransform ({});
    content.setBounds (0, 0, view->getWidth(), view->getHeight());
    content.setTransform (juce::AffineTransform::scale (s));
    setSize (juce::roundToInt ((float) view->getWidth() * s), juce::roundToInt ((float) view->getHeight() * s));
}

void NovaEditor::paint (juce::Graphics& g) { g.fillAll (bg); }
void NovaEditor::resized() {}

void NovaEditor::timerCallback()
{
    if (auto* t = dynamic_cast<Tickable*> (view.get())) t->tick();
}

bool NovaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;knova")) return true;
    return false;
}

void NovaEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    for (auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension ("knova"))
        {
            const auto k = proc.presets.importFile (f);
            if (k.isNotEmpty()) proc.loadPreset (k);
            if (auto* ev = dynamic_cast<ExtendedView*> (view.get())) ev->browser.refresh();
            continue;
        }
        int layer = 0;
        if (auto* ev = dynamic_cast<ExtendedView*> (view.get()))
        {
            const float s = proc.uiScale;
            layer = ev->layerAt (juce::Point<int> (juce::roundToInt ((float) x / s), juce::roundToInt ((float) y / s)));
        }
        juce::String err;
        if (! proc.loadSampleFile (layer, f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Nova", err);
        break;
    }
}
