#include "../../Source/GatePatterns.h"
#include "PluginEditor.h"
#include <complex>
#include <juce_dsp/juce_dsp.h>

using namespace nova;
using namespace nova::ui;

namespace
{
    // nastroje v menu podla skupin (cisla = hodnota parametra "inst", poradie v subore sa nemeni)
    struct InstGroup { const char* sk; const char* en; std::vector<int> ids; };
    const std::vector<InstGroup>& instGroups()
    {
        static const std::vector<InstGroup> g {
            { "Klávesy", "Keys", { 0, 1, 2, 3, 23 } },
            { "Gitary a basy", "Guitars & basses", { 4, 5, 33, 6, 7 } },
            { "Sláčiky a harfa", "Strings & harp", { 8, 9, 10, 20, 28, 29, 22 } },
            { "Plechové dychy", "Brass", { 12, 11, 14, 24, 27 } },
            { "Drevené dychy", "Woodwinds", { 15, 35, 16, 25, 26, 13, 34, 21 } },
            { "Melodické bicie a zvony", "Mallets & bells", { 17, 18, 32, 19, 31, 30 } } };
        return g;
    }

    constexpr int SW = 780, SH = 446;       // jednoducha verzia
    constexpr int EW = 1322, EH = 886;      // rozsirena verzia
    const juce::Colour arcB (0xff9d93c4);

    struct Tickable { virtual ~Tickable() = default; virtual void tick() = 0; };

    // "Prekvap ma": zvolena kategoria (-1 = cokolvek), menu na vyber a tlacidlo Obmena
    int surpriseType (NovaProcessor& p) { const auto s = p.presets.setting ("surpriseCat"); return s.isEmpty() ? -1 : juce::jlimit (-1, 7, s.getIntValue()); }
    juce::String surpriseTip (NovaProcessor& p)
    {
        const int t = surpriseType (p);
        return T ("Vymyslí úplne nový zvuk") + (t >= 0 ? " (" + T (NovaProcessor::surpriseTypes()[t]) + ")" : juce::String()) + T (". Stlač znova pre ďalší, Späť vráti predošlý. ▾ = vybrať druh zvuku.");
    }
    void surpriseMenu (NovaProcessor& p, juce::Component& target, std::function<void()> after)
    {
        juce::PopupMenu m;
        const int cur = surpriseType (p);
        m.addSectionHeader (T ("Prekvap ma – druh zvuku"));
        m.addItem (100, T ("Čokoľvek"), true, cur < 0);
        for (int i = 0; i < NovaProcessor::surpriseTypes().size(); ++i) m.addItem (101 + i, T (NovaProcessor::surpriseTypes()[i]), true, cur == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [&p, after] (int r)
        {
            if (r < 100) return;
            p.presets.setSetting ("surpriseCat", juce::String (r - 101));
            p.surprise (r - 101);
            if (after) after();
        });
    }
    void setupSurprise (NovaProcessor& p, juce::TextButton& sur, juce::TextButton& cat, juce::TextButton& mut, std::function<void()> after)
    {
        sur.setButtonText (T ("Prekvap ma")); sur.setTooltip (surpriseTip (p));
        sur.onClick = [&p, after] { p.surprise (surpriseType (p)); if (after) after(); };
        cat.setButtonText (juce::String::charToString (0x25BE)); cat.setTooltip (T ("Vybrať druh zvuku pre Prekvap ma"));
        cat.onClick = [&p, &cat, &sur, after] { surpriseMenu (p, cat, [&p, &sur, after] { sur.setTooltip (surpriseTip (p)); if (after) after(); }); };
        mut.setButtonText (T ("Obmena")); mut.setTooltip (T ("Jemne pozmení aktuálny zvuk – stlač viackrát, Späť vráti predošlý"));
        mut.onClick = [&p, after] { p.mutate(); if (after) after(); };
    }

    // FX: vsetky efekty zap / vyp - len na pocuvanie, zvuk sa nemeni
    void setupFx (NovaProcessor& p, juce::TextButton& b, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att)
    {
        b.setButtonText ("FX");
        b.setClickingTogglesState (true);
        b.setTooltip (T ("Efekty zap / vyp: delay, dozvuk, EQ, modulácia, drive, Tube a arpeggiátor. Len na počúvanie – zvuk sa nemení."));
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a2029));
        b.setColour (juce::TextButton::buttonOnColourId, steel);
        b.setColour (juce::TextButton::textColourOffId, dim);
        b.setColour (juce::TextButton::textColourOnId, text);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.params, "fx_all", b);
    }

    // citlivost kláves: ikona klaviatury, klik = ponuka jemna / normalna / tvrda
    struct KeysButton : juce::TextButton
    {
        KeysButton (NovaProcessor& pr) : p (pr)
        {
            setTooltip (T ("Citlivosť kláves – ako silno treba udrieť"));
            onClick = [this]
            {
                juce::PopupMenu m;
                const char* n[] = { "Jemná – aj slabší úder znie silno", "Normálna", "Tvrdá – na silný zvuk treba udrieť silno" };
                for (int i = 0; i < 3; ++i) m.addItem (i + 1, T (n[i]), true, p.velCurve.load() == i);
                juce::Component::SafePointer<KeysButton> sp (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp] (int r) { if (sp != nullptr && r > 0) { sp->p.setVelCurve (r - 1); sp->repaint(); } });
            };
        }
        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), hover, down);
            auto r = getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 12.0f);
            g.setColour (nova::ui::text.withAlpha (0.85f)); g.drawRoundedRectangle (r, 1.0f, 1.2f);
            for (int i = 1; i < 5; ++i) { const float x = r.getX() + r.getWidth() * (float) i / 5.0f; g.drawLine (x, r.getY(), x, r.getBottom(), 1.0f); }
            if (p.velCurve.load() != 1) { g.setColour (dim); g.setFont (juce::FontOptions (9.0f)); g.drawText (p.velCurve.load() == 0 ? "J" : "T", getLocalBounds().removeFromRight (7).removeFromBottom (11), juce::Justification::centred); }
        }
        NovaProcessor& p;
    };

    // ukazka zvuku pri vybere zo zoznamu: zap / vyp
    void setupPreview (NovaProcessor& p, juce::TextButton& b)
    {
        b.setButtonText (juce::String::charToString (0x266A));
        b.setClickingTogglesState (true);
        b.setTooltip (T ("Ukážka: pri výbere zvuku zo zoznamu krátko zahrá"));
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a2029));
        b.setColour (juce::TextButton::buttonOnColourId, steel);
        b.setColour (juce::TextButton::textColourOffId, dim);
        b.setColour (juce::TextButton::textColourOnId, text);
        b.setToggleState (p.previewOn(), juce::dontSendNotification);
        b.onClick = [&p, &b] { p.presets.setSetting ("preview", b.getToggleState() ? "1" : "0"); };
    }

    // "Nauc sa zvuk": v Korgus Sequenceri otvori okno ucenia zvuku z inej stopy
    void setupLearn (NovaProcessor& p, juce::TextButton& b)
    {
        b.setButtonText (T ("Nauč sa zvuk"));
        b.setTooltip (T ("Inspiria sa pokúsi napodobniť nástroj z inej stopy v Korgus Sequenceri."));
        b.onClick = [&p]
        {
            if (! p.requestLearn())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Korgus Inspiria",
                    T ("Nauč sa zvuk funguje v programe Korgus Sequencer: Inspiria tam napodobní nástroj z inej stopy."));
        };
    }


    // ===================================================================== jednoducha verzia
    // ===================================================================== velke tlacidlo "Inspiracia"
    // zachyti, co si prave zahral: v Korgus Sequenceri to ide do stopy, inde sa ponukne ulozenie MIDI / WAV
    struct InspireButton : juce::Component, juce::SettableTooltipClient, private juce::Timer
    {
        InspireButton (NovaProcessor& pr) : p (pr)
        {
            setTooltip (T ("Zachytí, čo si práve zahral (aj bez nahrávania) – v Korgus Sequenceri to vloží do stopy, inde ponúkne uloženie MIDI alebo WAV"));
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        ~InspireButton() override { stopTimer(); }
        void flash (const juce::String& m, int ms = 3500) { msg = m; startTimer (ms); repaint(); }
        void timerCallback() override { stopTimer(); if (! busy) { msg = {}; repaint(); } }
        void paint (juce::Graphics& g) override
        {
            const juce::Colour gold (0xffc9a35a);
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (hover ? juce::Colour (0xff2a2a26) : juce::Colour (0xff1f252d)); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (gold.withAlpha (hover ? 1.0f : 0.85f)); g.drawRoundedRectangle (r, 3.0f, 1.5f);
            g.setColour (gold);
            g.setFont (juce::FontOptions (msg.isEmpty() ? (getHeight() < 36 ? 14.0f : 17.0f) : (getHeight() < 36 ? 12.0f : 14.0f), juce::Font::bold));
            g.drawFittedText (msg.isEmpty() ? T ("✦ Inšpirácia") : msg, getLocalBounds().reduced (6, 2), juce::Justification::centred, 2);
        }
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && contains (e.getPosition()) && ! busy) capture(); }

        void capture()
        {
            auto ph = p.lastPhrase();
            if (ph.notes.empty()) { flash (T ("Zatiaľ si nič nezahral")); return; }
            if (NovaProcessor::hostedInKorgus()) { p.publishCapture (ph); flash (T ("Pridané do stopy")); return; }
            juce::PopupMenu m;
            m.addItem (1, T ("Uložiť ako MIDI…"));
            m.addItem (2, T ("Uložiť ako WAV…"));
            juce::Component::SafePointer<InspireButton> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp, ph] (int r) { if (sp != nullptr && r > 0) sp->save (ph, r == 2); });
        }
        void save (const nova::CapPhrase& ph, bool wav)
        {
            auto dir = dataDir(); dir.createDirectory();
            const auto name = "Inspiracia-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + (wav ? ".wav" : ".mid");
            chooser = std::make_unique<juce::FileChooser> (wav ? T ("Uložiť ako WAV…") : T ("Uložiť ako MIDI…"), dir.getChildFile (name), wav ? "*.wav" : "*.mid");
            juce::Component::SafePointer<InspireButton> sp (this);
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [sp, ph, wav] (const juce::FileChooser& fc)
            {
                if (sp == nullptr) return;
                auto f = fc.getResult();
                if (f == juce::File()) return;
                f = f.withFileExtension (wav ? "wav" : "mid");
                f.getParentDirectory().createDirectory();
                auto done = [sp, f] (bool ok)
                {
                    if (sp == nullptr) return;
                    sp->busy = false;
                    sp->flash (ok ? T ("Uložené: ") + f.getFileName() : T ("Nepodarilo sa uložiť"), 5000);
                };
                if (! wav) { done (writePhraseMidi (ph, f)); return; }
                sp->busy = true; sp->msg = T ("Ukladám…"); sp->repaint();
                auto settings = sp->p.currentSettings();
                std::array<UserSamplePtr, 2> smp { sp->p.synth.getUserSample (0), sp->p.synth.getUserSample (1) };
                juce::Thread::launch ([ph, settings, smp, f, done]
                {
                    const bool ok = renderPhraseWav (ph, settings, smp, f);
                    juce::MessageManager::callAsync ([done, ok] { done (ok); });
                });
            });
        }
        NovaProcessor& p;
        juce::String msg; bool hover = false, busy = false;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    // tlacidlo s ikonou velkosti okna (stvorec so sipkami do rohov)
    struct SizeIconButton : juce::Button
    {
        SizeIconButton() : juce::Button ("size") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (down ? steelHi : (over ? steel.brighter (0.15f) : juce::Colour (0xff1a2029))); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (over ? juce::Colour (0xffe4e9ef) : dim);
            const auto c = r.getCentre(); const float d = juce::jmin (r.getWidth(), r.getHeight()) * 0.28f, a = d * 0.55f;
            juce::Path p;
            for (int sx : { -1, 1 }) for (int sy : { -1, 1 })
            {
                const float x = c.x + sx * d, y = c.y + sy * d;
                p.startNewSubPath (x - sx * a, y); p.lineTo (x, y); p.lineTo (x, y - sy * a);
            }
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }
    };

    struct SimpleView : juce::Component, Tickable
    {
        struct PresetButton : juce::Component
        {
            juce::String name, key, cat; bool selected = false, hover = false, small = false, sound = false; std::function<void()> onClick;
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (selected ? steelHi : (hover ? steel.brighter (0.12f) : (small ? panel : steel)));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (selected ? juce::Colours::white : text.withAlpha (0.9f));
                g.setFont (juce::FontOptions (sound ? 11.5f : (small ? 13.0f : 14.0f), selected ? juce::Font::bold : juce::Font::plain));
                g.drawFittedText (name, getLocalBounds().reduced (5, sound ? 0 : 2), juce::Justification::centred, sound ? 1 : 2);
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
                b->name = T (f.name); b->key = "f:" + f.name; b->cat = f.category; b->sound = true;
                auto key = b->key;
                b->onClick = [this, key] { p.loadPreset (key); p.preview(); tick(); };
                addChildComponent (*b);
                buttons.push_back (std::move (b));
            }
            // kategorie (tovarenske, bez "Moje zvuky")
            for (int i = 0; i < categories().size() - 1; ++i)
            {
                auto c = std::make_unique<PresetButton>();
                c->name = T (categories()[i]); c->key = categories()[i]; c->small = true;
                auto cat = c->key;
                c->onClick = [this, cat] { setCat (cat); };
                addAndMakeVisible (*c);
                catButtons.push_back (std::move (c));
            }
            // dve casti: Nastroje (nahrate) a Syntezator
            for (int sct = 0; sct < 2; ++sct)
            {
                auto& b = secButtons[sct];
                b.name = T (sct == 0 ? "Nástroje" : "Syntezátor");
                b.onClick = [this, sct] { setSection (sct, true); };
                addAndMakeVisible (b);
            }
            {
                const auto* e = p.presets.find (p.currentKey);
                const auto saved = p.presets.setting ("simpleCat");
                juce::String c = e != nullptr && e->factory ? factoryPatches()[(size_t) e->factoryIndex].category : (saved.isNotEmpty() ? saved : categories()[0]);
                bool has = false; for (auto& cb : catButtons) has = has || cb->key == c;
                setCat (has ? c : categories()[0]);
            }
            for (auto id : { "bright", "attack", "release", "space", "width", "full", "volume" })
            {
                auto k = std::make_unique<Knob> (p.params, id, juce::String (id) == "release" ? T ("Dozvuk", "Release") : juce::String());
                k->label.setFont (juce::FontOptions (14.0f)); k->label.setColour (juce::Label::textColourId, text);
                addAndMakeVisible (*k);
                knobs.push_back (std::move (k));
            }
            ext.setButtonText (T ("Rozšírená"));
            ext.setTooltip (T ("Úplný editor zvuku: vrstvy, filtre, efekty, arpeggiátor, vlastné zvuky"));
            ext.onClick = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (1); }); };
            addAndMakeVisible (ext);
            sizeB.setTooltip (T ("Veľkosť okna"));
            sizeB.onClick = [this]
            {
                juce::PopupMenu m; const int cur = juce::roundToInt (p.uiScale * 100.0f);
                for (int v : { 75, 90, 100, 110, 125, 150, 175, 200 }) m.addItem (v, juce::String (v) + " %", true, v == cur);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeB), [sp = juce::Component::SafePointer<NovaEditor> (&ed)] (int r)
                { if (sp != nullptr && r > 0) sp->setScale ((float) r / 100.0f); });
            };
            addAndMakeVisible (sizeB);
            setupSurprise (p, surprise, surCat, mutateB, [this] { tick(); });
            addAndMakeVisible (surprise); addAndMakeVisible (surCat); addAndMakeVisible (mutateB);
            setupLearn (p, learnB); addAndMakeVisible (learnB);
            setupFx (p, fxB, fxAtt); addAndMakeVisible (fxB);
            setupPreview (p, pvB); pvB.setButtonText (juce::String::fromUTF8 ("♪ ") + T ("Ukážka")); addAndMakeVisible (pvB);
            addAndMakeVisible (insp);
            setSize (SW, SH);
            setSection (juce::jmax (0, section), false);   // tlacidla syntezatora pridane az po vybere kategorie
            tick();
        }
        void setSection (int sct, bool pickCat)
        {
            section = sct;
            for (int i = 0; i < 2; ++i) { secButtons[i].selected = i == sct; secButtons[i].repaint(); }
            for (auto& cb : catButtons) cb->setVisible (categorySection (cb->key) == sct);
            // Prekvap ma, Obmena a Nauc sa zvuk patria k syntezatoru
            for (auto* c : { (juce::Component*) &surprise, (juce::Component*) &surCat, (juce::Component*) &mutateB, (juce::Component*) &learnB }) c->setVisible (sct == 1);
            if (pickCat && categorySection (curCat) != sct)
            {
                const auto saved = p.presets.setting (sct == 0 ? "simpleCatI" : "simpleCatS");
                juce::String c = categorySection (saved) == sct ? saved : juce::String();
                if (c.isEmpty()) for (auto& cb : catButtons) if (categorySection (cb->key) == sct) { c = cb->key; break; }
                setCat (c);
                return;
            }
            resized();
        }
        void setCat (const juce::String& c)
        {
            curCat = c; p.presets.setSetting ("simpleCat", c);
            const int sc = categorySection (c);
            if (sc >= 0) p.presets.setSetting (sc == 0 ? "simpleCatI" : "simpleCatS", c);
            if (sc >= 0 && sc != section) { setSection (sc, false); }
            for (auto& cb : catButtons) { cb->selected = cb->key == c; cb->repaint(); }
            for (auto& b : buttons) b->setVisible (b->cat == c);
            resized();
        }
        void tick() override
        {
            // pri prepnuti zvuku sipkami ukaz jeho kategoriu
            if (p.currentKey != lastKey)
            {
                lastKey = p.currentKey;
                if (const auto* e = p.presets.find (p.currentKey); e != nullptr && e->factory && factoryPatches()[(size_t) e->factoryIndex].category != curCat)
                {
                    const auto c = factoryPatches()[(size_t) e->factoryIndex].category;
                    for (auto& cb : catButtons) if (cb->key == c) { setCat (c); break; }
                }
            }
            for (auto& b : buttons) { const bool s = b->key == p.currentKey; if (s != b->selected) { b->selected = s; b->repaint(); } }
            juce::String st;
            if (p.synth.pianoFailed()) st = T ("Klavírne zvuky sa nepodarilo načítať");
            else if (! p.synth.pianoReady()) st = T ("Načítavam klavír…");
            else if (! p.currentKey.startsWith ("f:") && p.currentName.isNotEmpty()) st = T (p.currentName);
            if (st != status) { status = st; repaint(); }
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (bg);
            auto head = getLocalBounds().removeFromTop (44);
            g.setColour (panel); g.fillRect (head);
            g.setColour (text); g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
            g.drawText ("KORGUS INSPIRIA", head.reduced (16, 0), juce::Justification::centredLeft);
            g.setColour (dim); g.setFont (juce::FontOptions (13.0f));
            g.drawText (status, head.reduced (16, 0).withTrimmedRight (600).withTrimmedLeft (190), juce::Justification::centredRight);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (T ("Klavír: Steinway B z VCSL (Versilian Studios, CC0) · Rhodes: jRhodes3 (J. Learman, CC BY-NC 4.0) · ostatné: Versilian Studios, Karoryfer (CC0)"), getLocalBounds().removeFromBottom (22).reduced (16, 0), juce::Justification::centredLeft);
        }
        void resized() override
        {
            sizeB.setBounds (getWidth() - 16 - 28, 9, 28, 26);
            ext.setBounds (sizeB.getX() - 6 - 90, 9, 90, 26);
            fxB.setBounds (ext.getX() - 6 - 40, 9, 40, 26);
            pvB.setBounds (fxB.getX() - 4 - 74, 9, 74, 26);
            kbB.setBounds (pvB.getX() - 4 - 30, 9, 30, 26);
            mutateB.setBounds (pvB.getX() - 8 - 76, 9, 76, 26);
            surCat.setBounds (mutateB.getX() - 6 - 22, 9, 22, 26);
            surprise.setBounds (surCat.getX() - 2 - 96, 9, 96, 26);
            learnB.setBounds (surprise.getX() - 8 - 100, 9, 100, 26);
            auto r = getLocalBounds().reduced (16, 0);
            r.removeFromTop (44 + 10);
            {
                auto sr = r.removeFromTop (28);
                const int sw = (sr.getWidth() - 6) / 2;
                secButtons[0].setBounds (sr.getX(), sr.getY(), sw, 28);
                secButtons[1].setBounds (sr.getX() + sw + 6, sr.getY(), sw, 28);
                r.removeFromTop (8);
            }
            {
                auto cr = r.removeFromTop (26);
                std::vector<PresetButton*> vis; for (auto& cb : catButtons) if (cb->isVisible()) vis.push_back (cb.get());
                const int n = (int) vis.size(), g2 = 4, cw = (cr.getWidth() - g2 * (n - 1)) / juce::jmax (1, n);
                for (int i = 0; i < n; ++i) vis[(size_t) i]->setBounds (cr.getX() + i * (cw + g2), cr.getY(), cw, 26);
            }
            r.removeFromTop (8);
            // zvuky vzdy rovnako male tlacidla (vyska ako pri 3 riadkoch po 5), aby nevyzerali ako dalsia uroven sekcii
            int nVis = 0; for (auto& b : buttons) if (b->isVisible()) ++nVis;
            const int cols = 5, gap = 5, rows = juce::jmax (3, (nVis + cols - 1) / cols), bh = juce::jmin (16, (2 * (42 + 6) - gap * rows) / rows);   // zvuky polovicne oproti kategoriam
            const int bw = (r.getWidth() - gap * (cols - 1)) / cols;
            int k = 0;
            for (auto& b : buttons)
                if (b->isVisible()) { b->setBounds (r.getX() + (k % cols) * (bw + gap), r.getY() + (k / cols) * (bh + gap), bw, bh); ++k; }
            r.removeFromTop (2 * (42 + gap) + 6);
            r.removeFromTop (6);
            auto kr = r.removeFromTop (104);
            const int kw = kr.getWidth() / (int) knobs.size();
            for (auto& k : knobs) k->setBounds (kr.removeFromLeft (kw).reduced (18, 0));
            insp.setBounds (16, getHeight() - 22 - 8 - 30, 160, 30);
        }
        NovaEditor& ed; NovaProcessor& p;
        InspireButton insp { p };
        SizeIconButton sizeB;
        std::vector<std::unique_ptr<PresetButton>> buttons, catButtons;
        PresetButton secButtons[2];
        int section = -1;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::String curCat, lastKey;
        juce::TextButton ext, surprise, surCat, mutateB, learnB, fxB, pvB;
        KeysButton kbB { p };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fxAtt;
        juce::String status;
    };


    // ===================================================================== prehliadac WAV (s prehravanim)
    struct WavBrowser : juce::Component, juce::ListBoxModel, juce::TextEditor::Listener
    {
        struct Places : juce::ListBoxModel
        {
            WavBrowser& b; Places (WavBrowser& w) : b (w) {}
            int getNumRows() override { return (int) b.places.size(); }
            void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
            {
                if (! juce::isPositiveAndBelow (row, (int) b.places.size())) return;
                const auto& pl = b.places[(size_t) row];
                if (pl.second == juce::File()) { g.setColour (dim); g.setFont (juce::FontOptions (11.5f, juce::Font::bold)); g.drawText (pl.first, 8, 0, w - 8, h, juce::Justification::bottomLeft); return; }
                const bool cur = pl.second == b.dir;
                if (cur) { g.setColour (steelHi); g.fillRoundedRectangle (juce::Rectangle<float> (2, 1, (float) w - 4, (float) h - 2), 3.0f); }
                g.setColour (cur ? juce::Colours::white : text); g.setFont (juce::FontOptions (13.5f));
                g.drawText (pl.first, 10, 0, w - 14, h, juce::Justification::centredLeft, true);
                (void) sel;
            }
            void listBoxItemClicked (int row, const juce::MouseEvent&) override
            {
                if (juce::isPositiveAndBelow (row, (int) b.places.size()) && b.places[(size_t) row].second != juce::File()) b.openDir (b.places[(size_t) row].second);
            }
        };

        WavBrowser (NovaProcessor& pr, int layerIdx, std::function<void (const juce::File&, int)> load, std::function<void()> close)
            : p (pr), layer (layerIdx), onLoad (std::move (load)), onClose (std::move (close)), placesModel (*this)
        {
            setWantsKeyboardFocus (false);
            title = T ("Vybrať zvuk (WAV, MP3…)");
            closeB.setButtonText (juce::String::charToString (0x2715)); closeB.onClick = [this] { if (onClose) onClose(); };
            upB.setButtonText (juce::String::charToString (0x25B2)); upB.setTooltip (T ("O priečinok vyššie")); upB.onClick = [this] { goUp(); };
            favB.setTooltip (T ("Pridať / odobrať priečinok z obľúbených")); favB.onClick = [this] { toggleFav(); };
            search.setTextToShowWhenEmpty (T ("Hľadať v priečinku…"), dim); search.addListener (this); search.setFont (juce::FontOptions (13.5f));
            deep.setButtonText (T ("aj podpriečinky")); deep.setToggleState (p.presets.setting ("wavDeep") == "1", juce::dontSendNotification);
            deep.onClick = [this] { p.presets.setSetting ("wavDeep", deep.getToggleState() ? "1" : "0"); refresh(); };
            autoP.setButtonText (T ("Prehrať pri výbere")); autoP.setToggleState (p.presets.setting ("wavAuto") != "0", juce::dontSendNotification);
            autoP.onClick = [this] { p.presets.setSetting ("wavAuto", autoP.getToggleState() ? "1" : "0"); if (! autoP.getToggleState()) p.stopPreview(); };
            playB.onClick = [this] { if (p.previewPlaying()) p.stopPreview(); else { const auto f = selectedFile(); if (f.existsAsFile()) p.previewFile (f); } };
            for (int l = 0; l < 2; ++l)
            {
                loadB[l].setButtonText (T (l == 0 ? "Do vrstvy A" : "Do vrstvy B"));
                loadB[l].onClick = [this, l] { const auto f = selectedFile(); if (f.existsAsFile()) doLoad (f, l); };
            }
            if (layer < 0) { loadB[1].setVisible (false); loadB[0].setButtonText (T ("Načítať")); }
            cancelB.setButtonText (T ("Zrušiť")); cancelB.onClick = [this] { if (onClose) onClose(); };
            for (auto* c : std::initializer_list<juce::Component*> { &closeB, &upB, &favB, &search, &deep, &autoP, &playB, &loadB[0], &loadB[1], &cancelB, &list, &placeList, &pathL }) addAndMakeVisible (c);
            list.setModel (this); list.setRowHeight (24); list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1a2029));
            placeList.setModel (&placesModel); placeList.setRowHeight (24); placeList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            pathL.setFont (juce::FontOptions (13.0f)); pathL.setColour (juce::Label::textColourId, text); pathL.setMinimumHorizontalScale (0.6f);
            juce::File start (p.presets.setting ("wavDir"));
            if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            buildPlaces();
            openDir (start);
        }
        ~WavBrowser() override { p.stopPreview(); }

        void buildPlaces()
        {
            places.clear();
            places.push_back ({ T ("MIESTA"), {} });
            auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            places.push_back ({ T ("Hudba"), juce::File::getSpecialLocation (juce::File::userMusicDirectory) });
            places.push_back ({ T ("Plocha"), juce::File::getSpecialLocation (juce::File::userDesktopDirectory) });
            places.push_back ({ T ("Dokumenty"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory) });
            if (home.getChildFile ("Downloads").isDirectory()) places.push_back ({ T ("Stiahnuté"), home.getChildFile ("Downloads") });
            juce::Array<juce::File> roots; juce::File::findFileSystemRoots (roots);
            for (auto& r : roots) if (r.isDirectory()) places.push_back ({ r.getFullPathName(), r });
            const auto fav = favs();
            if (! fav.isEmpty())
            {
                places.push_back ({ T ("OBĽÚBENÉ"), {} });
                for (auto& f : fav) { juce::File d (f); if (d.isDirectory()) places.push_back ({ d.getFileName().isEmpty() ? f : d.getFileName(), d }); }
            }
            placeList.updateContent(); placeList.repaint();
        }
        juce::StringArray favs() const { return juce::StringArray::fromTokens (p.presets.setting ("wavFav"), "|", ""); }
        void toggleFav()
        {
            auto f = favs(); const auto path = dir.getFullPathName();
            if (f.contains (path)) f.removeString (path); else f.add (path);
            f.removeEmptyStrings();
            p.presets.setSetting ("wavFav", f.joinIntoString ("|"));
            buildPlaces(); updateFavButton();
        }
        void updateFavButton() { favB.setButtonText (juce::String::charToString (favs().contains (dir.getFullPathName()) ? 0x2605 : 0x2606)); }
        static bool isAudio (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"); }

        void openDir (const juce::File& d)
        {
            if (! d.isDirectory()) return;
            dir = d; search.setText ({}, juce::dontSendNotification);
            p.presets.setSetting ("wavDir", d.getFullPathName());
            pathL.setText (d.getFullPathName(), juce::dontSendNotification);
            updateFavButton(); refresh(); placeList.repaint();
            list.selectRow (0); list.scrollToEnsureRowIsOnscreen (0);
        }
        void goUp() { const auto par = dir.getParentDirectory(); if (par != dir) { const auto old = dir; openDir (par); for (int i = 0; i < (int) items.size(); ++i) if (items[(size_t) i] == old) { list.selectRow (i); list.scrollToEnsureRowIsOnscreen (i); } } }
        void refresh()
        {
            items.clear(); truncated = false;
            const auto q = search.getText().trim().toLowerCase();
            if (q.isNotEmpty() && deep.getToggleState())
            {
                int n = 0;
                for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
                {
                    const auto f = e.getFile();
                    if (isAudio (f) && f.getFileName().toLowerCase().contains (q)) { items.push_back (f); if (++n >= 1500) { truncated = true; break; } }
                }
            }
            else
            {
                juce::Array<juce::File> ds, fs;
                for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
                {
                    const auto f = e.getFile();
                    if (f.isHidden() || f.getFileName().startsWithChar ('.')) continue;
                    if (q.isNotEmpty() && ! f.getFileName().toLowerCase().contains (q)) continue;
                    if (e.isDirectory()) ds.add (f); else if (isAudio (f)) fs.add (f);
                }
                auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
                std::sort (ds.begin(), ds.end(), byName); std::sort (fs.begin(), fs.end(), byName);
                for (auto& f : ds) items.push_back (f);
                for (auto& f : fs) items.push_back (f);
            }
            list.updateContent(); list.repaint(); repaint();
        }
        juce::File selectedFile() const { const int r = list.getSelectedRow(); return juce::isPositiveAndBelow (r, (int) items.size()) ? items[(size_t) r] : juce::File(); }
        void doLoad (const juce::File& f, int l) { p.stopPreview(); if (onLoad) onLoad (f, l); }
        void textEditorTextChanged (juce::TextEditor&) override { refresh(); list.selectRow (0); }
        void textEditorReturnKeyPressed (juce::TextEditor&) override { list.grabKeyboardFocus(); }

        // ListBoxModel
        int getNumRows() override { return (int) items.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
            const auto& f = items[(size_t) row];
            const bool d = f.isDirectory();
            if (sel) { g.setColour (steelHi); g.fillRoundedRectangle (juce::Rectangle<float> (2, 1, (float) w - 4, (float) h - 2), 3.0f); }
            g.setColour (d ? amber.brighter (0.3f) : arc); g.setFont (juce::FontOptions (13.0f));
            g.drawText (juce::String::charToString (d ? 0x25B8 : 0x266A), 6, 0, 18, h, juce::Justification::centred);
            g.setColour (sel ? juce::Colours::white : text); g.setFont (juce::FontOptions (13.5f));
            const auto nm = (search.getText().isNotEmpty() && deep.getToggleState()) ? f.getRelativePathFrom (dir) : f.getFileName();
            g.drawText (nm, 28, 0, w - 110, h, juce::Justification::centredLeft, true);
            if (! d)
            {
                const auto sz = f.getSize();
                g.setColour (sel ? text : dim); g.setFont (juce::FontOptions (12.0f));
                g.drawText (sz >= 1048576 ? juce::String ((double) sz / 1048576.0, 1) + " MB" : juce::String (juce::jmax ((juce::int64) 1, sz / 1024)) + " kB", w - 80, 0, 72, h, juce::Justification::centredRight);
            }
        }
        void selectedRowsChanged (int row) override
        {
            if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
            const auto f = items[(size_t) row];
            if (f.existsAsFile() && autoP.getToggleState()) p.previewFile (f); else if (f.isDirectory()) p.stopPreview();
        }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { activate (row); }
        void returnKeyPressed (int row) override { activate (row); }
        void activate (int row)
        {
            if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
            const auto f = items[(size_t) row];
            if (f.isDirectory()) openDir (f); else doLoad (f, juce::jmax (0, layer));
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
            if (k == juce::KeyPress::backspaceKey && ! search.hasKeyboardFocus (true)) { goUp(); return true; }
            if (k == juce::KeyPress::spaceKey && ! search.hasKeyboardFocus (true)) { playB.triggerClick(); return true; }
            return true;   // sipky a ine klavesy neprepinaju zvuky pod oknom
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.55f));
            g.setColour (panel); g.fillRoundedRectangle (box.toFloat(), 3.0f);
            g.setColour (line); g.drawRoundedRectangle (box.toFloat(), 3.0f, 1.0f);
            g.setColour (text); g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (title + (layer >= 0 ? (layer == 0 ? T (" – vrstva A") : T (" – vrstva B")) : juce::String()), box.getX() + 14, box.getY() + 10, box.getWidth() - 60, 22, juce::Justification::centredLeft);
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            juce::String info = juce::String ((int) items.size()) + T (" položiek");
            if (truncated) info << T (" (zobrazené sú len prvé)");
            info << T ("  ·  ↑↓ výber, Enter načíta, medzerník prehrá, Backspace = vyššie");
            g.drawText (info, list.getBounds().translated (0, list.getHeight() + 4).withHeight (16), juce::Justification::centredLeft);
        }
        void resized() override
        {
            box = getLocalBounds().withSizeKeepingCentre (juce::jmin (860, getWidth() - 40), juce::jmin (620, getHeight() - 40));
            auto r = box.reduced (14, 10);
            auto top = r.removeFromTop (24); closeB.setBounds (top.removeFromRight (28));
            r.removeFromTop (8);
            auto left = r.removeFromLeft (170); r.removeFromLeft (10);
            placeList.setBounds (left);
            auto row1 = r.removeFromTop (26); upB.setBounds (row1.removeFromLeft (30)); row1.removeFromLeft (6); favB.setBounds (row1.removeFromRight (30)); row1.removeFromRight (6); pathL.setBounds (row1);
            r.removeFromTop (6);
            auto row2 = r.removeFromTop (26); deep.setBounds (row2.removeFromRight (140)); row2.removeFromRight (8); search.setBounds (row2);
            r.removeFromTop (8);
            auto bottom = r.removeFromBottom (30); r.removeFromBottom (24);
            list.setBounds (r);
            autoP.setBounds (bottom.removeFromLeft (170)); playB.setBounds (bottom.removeFromLeft (44).reduced (0, 1));
            cancelB.setBounds (bottom.removeFromRight (90)); bottom.removeFromRight (8);
            loadB[1].setBounds (bottom.removeFromRight (120)); bottom.removeFromRight (6); loadB[0].setBounds (bottom.removeFromRight (120));
        }
        void tickPlay() { const auto t = juce::String::charToString (p.previewPlaying() ? 0x25A0 : 0x25B6); if (playB.getButtonText() != t) playB.setButtonText (t); }

        NovaProcessor& p; int layer; std::function<void (const juce::File&, int)> onLoad; std::function<void()> onClose;
        Places placesModel; std::vector<std::pair<juce::String, juce::File>> places;
        juce::File dir; std::vector<juce::File> items; bool truncated = false; juce::String title; juce::Rectangle<int> box;
        juce::TextButton closeB, upB, favB, playB, loadB[2], cancelB; juce::ToggleButton deep, autoP;
        juce::TextEditor search; juce::ListBox list, placeList; juce::Label pathL;
    };

    // ===================================================================== displej hlasov (unison / wavetable / roj)
    struct VoiceDisplay : juce::Component, juce::SettableTooltipClient, juce::Timer
    {
        VoiceDisplay (NovaProcessor& pr, const juce::String& pre) : p (pr)
        {
            for (auto id : { "wave", "uni", "spread", "level", "color", "wt" }) src.push_back (p.params.getRawParameterValue (pre + id));
            setTooltip (T ("Displej hlasov: zvislé čiary = hlasy unisona (vodorovne rozladenie v centoch, výška = hlasitosť hlasu). Pri WT je v pozadí aktuálny tvar vlny, pri Roji alikvoty."));
            startTimerHz (25);
        }
        void timerCallback() override
        {
            if (! isShowing()) return;
            bool ch = false;
            for (size_t i = 0; i < src.size(); ++i) { const float v = src[i]->load(); if (v != last[i]) { last[i] = v; ch = true; } }
            if (ch) repaint();
        }
        void paint (juce::Graphics& g) override
        {
            const juce::Colour gold (0xffc9a35a), grid (0xff2d4362);
            const auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff141a21)); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            const int w = juce::roundToInt (src[0]->load()), n = juce::jlimit (1, 8, juce::roundToInt (src[1]->load()));
            const float spread = src[2]->load(), level = src[3]->load(), color = src[4]->load();
            const int tab = juce::jlimit (0, nova::WtBank::numTables - 1, juce::roundToInt (src[5]->load()));
            auto in = r.reduced (7.0f, 4.0f);
            auto head = in.removeFromTop (12.0f); in.removeFromTop (2.0f);
            const float x0 = in.getX(), y0 = in.getY(), pw = in.getWidth(), ph = in.getHeight(), yb = in.getBottom();
            const bool uni = w <= 3 || w == 9;
            // mriezka
            g.setColour (grid.withAlpha (0.8f));
            for (int i = 0; i <= 4; ++i) g.fillRect (x0 + pw * (float) i * 0.25f - (i == 4 ? 1.0f : 0.0f), y0, 1.0f, ph);
            g.fillRect (x0, yb - 1.0f, pw, 1.0f); g.fillRect (x0, y0 + ph * 0.5f, pw, 1.0f);
            juce::String left, right;
            if (w == 9)   // aktualny ramec wavetable (pozicia z gombika) v pozadi
            {
                const auto& bank = nova::WtBank::get();
                const float fp = juce::jlimit (0.0f, 1.0f, color) * (float) (nova::WtBank::numFrames - 1);
                const int fi = juce::jmin ((int) fp, nova::WtBank::numFrames - 2); const float ff = fp - (float) fi;
                const float* a = bank.frame (tab, 1, fi); const float* b = bank.frame (tab, 1, fi + 1);
                const int len = bank.len[1], pts = juce::jmax (8, (int) pw);
                juce::Path path;
                for (int i = 0; i <= pts; ++i)
                {
                    const int k = juce::jmin (len - 1, i * len / pts);
                    const float y = y0 + ph * 0.5f - (a[k] + ff * (b[k] - a[k])) * ph * 0.3f;
                    if (i == 0) path.startNewSubPath (x0, y); else path.lineTo (x0 + pw * (float) i / (float) pts, y);
                }
                g.setColour (steelHi.withAlpha (0.9f)); g.strokePath (path, juce::PathStrokeType (1.2f));
                if (const auto* sp = findSpec ("a_wt")) right = T (sp->choices[tab]) + " " + juce::String (juce::roundToInt (color * 100.0f)) + " %";
            }
            auto bar = [&] (float x, float amp, float thick)
            {
                const float h = juce::jlimit (2.0f, ph, ph * 0.96f * std::sqrt (juce::jmax (0.0f, amp)));
                g.fillRect (x - thick * 0.5f, yb - h, thick, h);
            };
            g.setColour (gold);
            if (w == 8)   // Roj: alikvoty (vodorovne cislo harmonickej, rozptyl podla Rozladenia)
            {
                const int N = 4 * n; const float B = 0.0012f * spread;
                for (int k = 0; k < N; ++k)
                {
                    const float kk = (float) (k + 1), sk = k == 0 ? 0.0f : 0.5f * (kk - 1.0f) / 8.0f + 0.5f * nova::rojScatter (k + 1);
                    bar (x0 + pw * (kk * (1.0f + B * sk) - 0.5f) / 33.5f, level * std::pow (kk, -1.25f), 2.0f);
                }
                left = T ("ALIKVOTY") + " " + juce::String (N);
            }
            else if (uni)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float pos = n > 1 ? (float) k / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
                    bar (x0 + pw * (0.5f + 0.5f * spread * pos / 50.0f), level / std::sqrt ((float) n), 2.0f);
                }
                left = "UNISON " + juce::String (n) + (n > 1 ? juce::String::fromUTF8 (" · ±") + juce::String (juce::roundToInt (spread)) + " ct" : juce::String());
                if (right.isEmpty()) right = juce::String::fromUTF8 ("±50 ct");
            }
            else { bar (x0 + pw * 0.5f, level, 2.0f); left = T ("1 HLAS"); }
            g.setFont (juce::FontOptions (10.5f)); g.setColour (dim);
            g.drawText (left, head, juce::Justification::centredLeft); g.drawText (right, head, juce::Justification::centredRight);
        }
        NovaProcessor& p; std::vector<std::atomic<float>*> src; float last[6] { -1, -1, -1, -1, -1, -1 };
    };

    // ===================================================================== vrstva A / B
    // ===================================================================== editor samplu (vlastny WAV)
    // nedestruktivne: zaciatok/koniec, nabeh/doznenie, slucka, odzadu; povodny subor ostane nezmeneny
    struct SampleEditor : juce::Component, juce::Timer
    {
        SampleEditor (NovaProcessor& pr, int layer, std::function<void()> changed) : p (pr), li (layer), pre (layer == 0 ? "a_" : "b_"), onChanged (std::move (changed))
        {
            ed = p.synth.getSmpEdit (li);
            smp = p.synth.getUserSample (li);
            buildPeaks();
            auto btn = [this] (juce::TextButton& b, const juce::String& t, const juce::String& tip)
            {
                b.setButtonText (t); b.setTooltip (tip); addAndMakeVisible (b);
            };
            btn (play, juce::String::fromUTF8 ("▶ ") + T ("Prehrať"), T ("Podrž = zahrá sampel na základnom tóne"));
            btn (rev, juce::String::fromUTF8 ("⇄ ") + T ("Odzadu"), T ("Prehrávať sampel odzadu"));
            btn (loopB, juce::String::fromUTF8 ("⟲ ") + T ("Slučka"), T ("Pri držaní tónu opakuje označený kúsok (svetlé pole)"));
            btn (pitch, juce::String::fromUTF8 ("♪ ") + T ("Zistiť tón"), T ("Zistí tón vybranej časti a nastaví Zákl. tón a Jemne"));
            btn (reset, T ("Pôvodné"), T ("Celý sampel bez úprav"));
            rev.setClickingTogglesState (true); loopB.setClickingTogglesState (true);
            rev.setToggleState (ed.rev, juce::dontSendNotification);
            loopB.setToggleState (p.params.getRawParameterValue (pre + "loop")->load() > 0.5f, juce::dontSendNotification);
            play.onStateChange = [this]
            {
                const bool d = play.isDown();
                if (d == playing) return;
                playing = d;
                const int root = juce::roundToInt (p.params.getRawParameterValue (pre + "root")->load());
                if (d) { playNote = root; p.keyboard.noteOn (1, playNote, 0.85f); }
                else p.keyboard.noteOff (1, playNote, 0.0f);
            };
            rev.onClick = [this] { ed.rev = rev.getToggleState(); apply(); };
            loopB.onClick = [this] { if (auto* prm = p.params.getParameter (pre + "loop")) prm->setValueNotifyingHost (loopB.getToggleState() ? 1.0f : 0.0f); repaint(); };
            reset.onClick = [this] { const bool r = ed.rev; ed = {}; ed.rev = r; apply(); };
            pitch.onClick = [this]
            {
                if (smp == nullptr) return;
                const int n = smp->buf.getNumSamples(), a = (int) (ed.start * (float) n), b = (int) (ed.end * (float) n);
                juce::AudioBuffer<float> part (smp->buf.getNumChannels(), juce::jmax (1, b - a));
                for (int c = 0; c < part.getNumChannels(); ++c) part.copyFrom (c, 0, smp->buf, c, a, part.getNumSamples());
                if (ed.rev) part.reverse (0, part.getNumSamples());
                const float m = NovaProcessor::detectPitch (part, smp->rate);
                if (m <= 0) { info = T ("Tón sa nedá zistiť (bicie alebo šum)."); repaint(); return; }
                const int root = juce::jlimit (0, 127, juce::roundToInt (m));
                if (auto* prm = p.params.getParameter (pre + "root")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) root));
                if (auto* prm = p.params.getParameter (pre + "fine")) prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-50.0f, 50.0f, ((float) root - m) * 100.0f)));
                info = T ("Tón: ") + juce::MidiMessage::getMidiNoteName (root, true, true, 4);
                repaint(); if (onChanged) onChanged();
            };
            setSize (620, 270);
        }
        ~SampleEditor() override { if (playing) p.keyboard.noteOff (1, playNote, 0.0f); }
        void timerCallback() override { stopTimer(); }

        void buildPeaks()
        {
            peaks.clear();
            if (smp == nullptr) return;
            const int n = smp->buf.getNumSamples(), W = 600;
            peaks.resize ((size_t) W);
            for (int x = 0; x < W; ++x)
            {
                const int a = (int) ((juce::int64) n * x / W), b = juce::jmax (a + 1, (int) ((juce::int64) n * (x + 1) / W));
                float lo = 0, hi = 0;
                for (int c = 0; c < smp->buf.getNumChannels(); ++c)
                {
                    const auto r = smp->buf.findMinMax (c, a, juce::jmin (b, n) - a);
                    lo = juce::jmin (lo, r.getStart()); hi = juce::jmax (hi, r.getEnd());
                }
                peaks[(size_t) x] = { lo, hi };
            }
        }
        juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().reduced (12.0f).withTrimmedTop (24.0f).withTrimmedBottom (44.0f); }
        float xOf (float f) const { const auto r = waveArea(); return r.getX() + f * r.getWidth(); }
        float fOf (float x) const { const auto r = waveArea(); return juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()); }
        double durSec() const { return smp != nullptr ? smp->buf.getNumSamples() / smp->rate : 1.0; }
        bool loopOn() const { return p.params.getRawParameterValue (pre + "loop")->load() > 0.5f; }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (bg);
            g.setColour (text); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (T ("Editor samplu") + "  " + (smp != nullptr ? smp->name : juce::String()), 12, 4, getWidth() - 24, 20, juce::Justification::centredLeft);
            const auto r = waveArea();
            g.setColour (panel); g.fillRoundedRectangle (r, 3.0f);
            if (peaks.empty()) { g.setColour (dim); g.drawText (T ("Žiadny sampel"), r, juce::Justification::centred); return; }
            const float cy = r.getCentreY(), hh = r.getHeight() * 0.46f;
            // slucka
            if (loopOn()) { g.setColour (steelHi.withAlpha (0.25f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (ed.ls), r.getY(), xOf (ed.le), r.getBottom())); }
            // vlna (vybrana cast jasnejsie)
            for (int x = 0; x < (int) peaks.size(); ++x)
            {
                const float f = ((float) x + 0.5f) / (float) peaks.size(), px = r.getX() + f * r.getWidth();
                const bool in = f >= ed.start && f <= ed.end;
                g.setColour (in ? arc : dim.withAlpha (0.35f));
                const float lo = peaks[(size_t) x].first, hi = peaks[(size_t) x].second;
                g.drawVerticalLine ((int) px, cy - hi * hh, cy - lo * hh + 1.0f);
            }
            // stmavenie mimo vyberu
            g.setColour (bg.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), xOf (ed.start), r.getBottom()));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (ed.end), r.getY(), r.getRight(), r.getBottom()));
            // nabeh a doznenie (ciara hlasitosti)
            const float d = (float) durSec(), xs = xOf (ed.start), xe = xOf (ed.end);
            const float xi = juce::jmin (xe, xs + (ed.fin / d) * r.getWidth()), xo = juce::jmax (xi, xe - (loopOn() ? 0.0f : (ed.fout / d) * r.getWidth()));
            juce::Path env; env.startNewSubPath (xs, r.getBottom()); env.lineTo (xi, r.getY() + 3); env.lineTo (xo, r.getY() + 3); env.lineTo (xe, loopOn() ? r.getY() + 3 : r.getBottom());
            g.setColour (amber); g.strokePath (env, juce::PathStrokeType (1.6f));
            auto knob = [&] (float x, float y) { g.setColour (amber); g.fillRect (x - 4, y - 4, 8.0f, 8.0f); };
            knob (xi, r.getY() + 3); if (! loopOn()) knob (xo, r.getY() + 3);
            // znacky
            auto mark = [&] (float x, juce::Colour c, const juce::String& t)
            {
                g.setColour (c); g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
                g.fillRect (x - 9.0f, r.getBottom() - 14.0f, 18.0f, 14.0f);
                g.setColour (bg); g.setFont (juce::FontOptions (10.0f, juce::Font::bold)); g.drawText (t, (int) x - 9, (int) r.getBottom() - 14, 18, 14, juce::Justification::centred);
            };
            mark (xs, text, "Z"); mark (xe, text, "K");
            if (loopOn()) { mark (xOf (ed.ls), steelHi.brighter (0.4f), "L"); mark (xOf (ed.le), steelHi.brighter (0.4f), "L"); }
            // udaje
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            auto ms = [] (double sec) { return sec < 1.0 ? juce::String (juce::roundToInt (sec * 1000.0)) + " ms" : juce::String (sec, 2) + " s"; };
            juce::String t = T ("Začiatok ") + ms (ed.start * d) + "   " + T ("Koniec ") + ms (ed.end * d) + "   " + T ("Nábeh ") + ms (ed.fin) + (loopOn() ? juce::String() : "   " + T ("Doznenie ") + ms (ed.fout));
            if (info.isNotEmpty()) t += "   ·   " + info;
            g.drawText (t, 12, getHeight() - 70, getWidth() - 24, 18, juce::Justification::centredLeft);
        }
        void resized() override
        {
            int x = 12; const int y = getHeight() - 38, h = 28;
            for (auto* b : { &play, &rev, &loopB, &pitch }) { b->setBounds (x, y, 112, h); x += 118; }
            reset.setBounds (getWidth() - 12 - 90, y, 90, h);
        }
        int hit (juce::Point<float> m) const
        {
            const auto r = waveArea(); const float d = (float) durSec();
            const float xs = xOf (ed.start), xe = xOf (ed.end), xi = juce::jmin (xe, xs + (ed.fin / d) * r.getWidth()), xo = juce::jmax (xi, xe - (ed.fout / d) * r.getWidth());
            if (m.y < r.getY() + 14)
            {
                if (std::abs (m.x - xi) < 9) return 4;
                if (! loopOn() && std::abs (m.x - xo) < 9) return 5;
            }
            struct C { float x; int id; };
            std::vector<C> c { { xs, 0 }, { xe, 1 } };
            if (loopOn()) { c.push_back ({ xOf (ed.ls), 2 }); c.push_back ({ xOf (ed.le), 3 }); }
            int best = -1; float bd = 9;
            for (auto& k : c) if (std::abs (m.x - k.x) < bd) { bd = std::abs (m.x - k.x); best = k.id; }
            return best;
        }
        void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (hit (e.position) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
        void mouseDown (const juce::MouseEvent& e) override { drag = hit (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (drag < 0) return;
            const float f = fOf (e.position.x), d = (float) durSec(), minGap = 0.002f;
            switch (drag)
            {
                case 0: ed.start = juce::jlimit (0.0f, ed.end - minGap, f); break;
                case 1: ed.end = juce::jlimit (ed.start + minGap, 1.0f, f); break;
                case 2: ed.ls = juce::jlimit (0.0f, ed.le - minGap, f); break;
                case 3: ed.le = juce::jlimit (ed.ls + minGap, 1.0f, f); break;
                case 4: ed.fin = juce::jlimit (0.0f, (ed.end - ed.start) * d, (f - ed.start) * d); break;
                case 5: ed.fout = juce::jlimit (0.0f, (ed.end - ed.start) * d, (ed.end - f) * d); break;
                default: break;
            }
            apply();
        }
        void mouseUp (const juce::MouseEvent&) override { drag = -1; }
        void apply() { p.synth.setSmpEdit (li, ed); repaint(); if (onChanged) onChanged(); }

        NovaProcessor& p; int li; juce::String pre; std::function<void()> onChanged;
        nova::SmpEdit ed; nova::UserSamplePtr smp; std::vector<std::pair<float, float>> peaks;
        juce::TextButton play, rev, loopB, pitch, reset;
        juce::String info; int drag = -1, playNote = 60; bool playing = false;
    };

    struct LayerPanel : juce::Component
    {
        LayerPanel (NovaProcessor& pr, int layer) : p (pr), li (layer), pre (layer == 0 ? "a_" : "b_"),
            on (pr.params, pre + "on", layer == 0 ? T ("VRSTVA A") : T ("VRSTVA B"), layer == 0 ? arc : arcB),
            wave (pr.params, pre + "wave", juce::StringArray::fromTokens (T ("Sínus|Trojuholník|Píla|Obdĺžnik|Šum|Pluck|FM|Sampel|Roj|WT"), "|", ""), layer == 0 ? steelHi : accentB),
            ftype (pr.params, pre + "ftype", juce::StringArray::fromTokens (T ("Dolná|Pásmová|Horná|Telo"), "|", ""), layer == 0 ? steelHi : accentB),
            f24 (pr.params, pre + "f24", "24 dB", layer == 0 ? arc : arcB),
            lshape (pr.params, pre + "lshape", juce::StringArray::fromTokens (T ("Sínus|Trojuholník|Obdĺžnik|Náhodne"), "|", ""), layer == 0 ? steelHi : accentB),
            ltgt (pr.params, pre + "ltgt"), ratio (pr.params, pre + "ratio"),
            src (pr.params, pre + "src", juce::StringArray::fromTokens (T ("Nástroj|Vzorky|WAV"), "|", ""), layer == 0 ? steelHi : accentB),
            loop (pr.params, pre + "loop", T ("Opakovať"), layer == 0 ? arc : arcB),
            wt (pr.params, pre + "wt"), disp (pr, pre)
        {
            addAndMakeVisible (wt); addAndMakeVisible (wtLabel); addAndMakeVisible (disp);
            wtLabel.setText (T ("Tabuľka"), juce::dontSendNotification); wtLabel.setFont (juce::FontOptions (12.5f));
            wtLabel.setColour (juce::Label::textColourId, dim.brighter (0.2f)); wtLabel.setJustificationType (juce::Justification::centred);
            wt.setTooltip (T ("Tabuľka tvarov vlny: Analóg (píla–obdĺžnik–pulz), PWM, Harmonický (rezonančný prechod), Vokál (A-E-I-O-U), Digitál, Organ, Sklo, Spev"));
            ltgt.setTooltip (T ("Cieľ LFO: Výška = vibrato, Filter, Hlasitosť = tremolo, Panoráma, Farba = hýbe gombíkom Farba (šírka pulzu, FM index, Focus, pozícia WT)"));
            // prepinace s ikonami
            wave.setIcons ({ icSine, icTri, icSaw, icSquare, -1, -1, -1, -1, -1, -1 });
            wave.setWeights ({ 0.7f, 0.7f, 0.7f, 0.7f, 1.05f, 1.15f, 0.85f, 1.35f, 0.95f, 0.95f });
            wave.setGroupEnd (3);
            wave.setTips ({ T ("Analóg – sínus (mäkký, čistý)"), T ("Analóg – trojuholník (jemný, flauta)"), T ("Analóg – píla (ostrý, struny, brass)"), T ("Analóg – obdĺžnik (dutý, klarinet)"),
                            T ("Šum (dych, vietor, bicie)"), T ("Pluck – brnknutá struna"), T ("FM – zvonivé a elektrické piano"), T ("Sampel – klavír alebo vlastný WAV"),
                            T ("Roj – veľa rozladených hlasov (supersaw)"), T ("Wavetable – plynulý prechod medzi tvarmi vlny") });
            ftype.setIcons ({ icLow, icBand, icHigh, icBody });
            ftype.setTips ({ T ("Dolná priepusť – stmavuje (najčastejší)"), T ("Pásmová priepusť – nechá len stred"), T ("Horná priepusť – zoslabí basy"), T ("Telo – rezonancie ako drevo nástroja") });
            lshape.setIcons ({ icSine, icTri, icSquare, icRandom });
            lshape.setTips ({ T ("LFO sínus"), T ("LFO trojuholník"), T ("LFO obdĺžnik"), T ("LFO náhodne") });
            src.setValueMap ({ 0, 2, 1 });   // parameter: 0 klavir, 1 WAV, 2 vzorky
            src.setTips ({ T ("Realistické nahraté nástroje – klavír, Rhodes, gitary, basy, sláčiky, dychy, mallets"), T ("Jednoduché vzorky – organ, zbor, zvon, synth…"), T ("Vlastný zvuk WAV") });
            src.onPick = [this] (int v) { p.setSourceMode (li, v); update(); };
            addAndMakeVisible (on); addAndMakeVisible (wave); addAndMakeVisible (ftype); addAndMakeVisible (f24);
            addAndMakeVisible (lshape); addAndMakeVisible (ltgt); addAndMakeVisible (ratio); addAndMakeVisible (src); addAndMakeVisible (loop);
            const char* r1[] = { "oct", "semi", "fine", "uni", "spread", "level", "pan", "penv", "ptime" };
            const char* r2[] = { "cut", "reso", "fenv", "ktrk", "fvel", "fa", "fd", "fs", "fr" };
            const char* r3[] = { "aa", "ad", "as", "ar", "lrate", "lsync", "ldepth", "ldelay" };
            const char* t1[] = { "Oktáva", "Poltón", "Jemne", "Unison", "Rozlad.", "Úroveň", "Panoráma", "Skok", "Čas" };
            const char* t2[] = { "Cutoff", "Rezon.", "Obálka", "Kláves", "Úder", "Nábeh", "Pokles", "Držanie", "Doznenie" };
            const char* t3[] = { "Nábeh", "Pokles", "Držanie", "Doznenie", "Rýchlosť", "Tempo", "Hĺbka", "Onesk." };
            for (int i = 0; i < 9; ++i) { row1.push_back (mk (r1[i], T (t1[i]))); row2.push_back (mk (r2[i], T (t2[i]))); }
            for (int i = 0; i < 8; ++i) row3.push_back (mk (r3[i], T (t3[i])));
            row3[5]->s.textFromValueFunction = [] (double v)
            {
                static const char* c[] = { "Voľne", "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T" };
                return T (c[juce::jlimit (0, 8, (int) std::round (v))]);
            };
            row3[5]->s.updateText();
            row3[5]->s.setTooltip (T ("LFO podľa tempa piesne (1/8 = vlnenie každú osminu, T = triola). Voľne = podľa gombíka Rýchlosť."));
            row1[7]->s.setTooltip (T ("Skok výšky na začiatku tónu (v poltónoch) – úder bubna, trsatko, „pew“ zvuky"));
            row1[8]->s.setTooltip (T ("Ako rýchlo sa výška vráti na tón"));
            row2[3]->s.setTooltip (T ("Vyššie tóny majú otvorenejší filter (0 = rovnako pre všetky)"));
            row2[4]->s.setTooltip (T ("Silnejší úder otvorí filter viac (0 = nezáleží na sile)"));
            noise = mk ("noise", T ("Šum"));
            noise->s.setTooltip (T ("Primieša šum do vrstvy – dych pri flaute, škrabnutie trsatka, vzduch"));
            color = mk ("color", {});
            root = mk ("root", T ("Zákl. tón"));
            load.setButtonText (T ("Načítať…"));
            load.setTooltip (T ("Vlastný zvuk do vrstvy (alebo pretiahni WAV súbor myšou na vrstvu)"));
            load.onClick = [this] { chooseFile(); };
            addAndMakeVisible (load);
            // zabudovane vzorky: klik na nazov = zoznam vsetkych
            nameBtn.setTooltip (T ("Klik = zoznam vzoriek"));
            nameBtn.onClick = [this]
            {
                juce::PopupMenu m;
                if (instMode())
                {
                    const int cur = curInst();
                    for (const auto& g : instGroups())
                    {
                        m.addSectionHeader (juce::String::fromUTF8 (lang() == 1 ? g.en : g.sk));
                        for (int i : g.ids) if (juce::isPositiveAndBelow (i, InstrBank::numInstr)) m.addItem (i + 1, InstrBank::name (i, lang() == 1), true, i == cur);
                    }
                    juce::Component::SafePointer<LayerPanel> sp (this);
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameBtn), [sp] (int r)
                    {
                        if (sp == nullptr || r <= 0) return;
                        sp->setInst (r - 1);
                    });
                    return;
                }
                const int cur = p.builtinSample (li);
                for (int i = 0; i < NovaProcessor::builtinSampleCount(); ++i) m.addItem (i + 1, NovaProcessor::builtinSampleName (i), true, i == cur);
                juce::Component::SafePointer<LayerPanel> sp (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameBtn), [sp] (int r)
                {
                    if (sp == nullptr || r <= 0) return;
                    sp->p.loadBuiltinSample (sp->li, r - 1);
                    sp->update();
                });
            };
            addChildComponent (nameBtn);
            smpName.setFont (juce::FontOptions (12.5f)); smpName.setColour (juce::Label::textColourId, dim.brighter (0.3f));
            addAndMakeVisible (smpName);
            // prehadzovac samplov: predchadzajuci / dalsi zvuk z toho isteho priecinka
            for (int d = 0; d < 2; ++d)
            {
                stepB[d].setButtonText (juce::String::charToString (d == 0 ? 0x25C0 : 0x25B6));
                stepB[d].setTooltip (T (d == 0 ? "Predchádzajúci zvuk" : "Ďalší zvuk"));
                stepB[d].onClick = [this, d]
                {
                    if (instMode())
                    {   // dalsi / predosly nastroj v poradi ako v menu (podla skupin)
                        std::vector<int> ord; for (const auto& g : instGroups()) for (int i : g.ids) ord.push_back (i);
                        auto it = std::find (ord.begin(), ord.end(), curInst());
                        const int k = it == ord.end() ? 0 : (int) (it - ord.begin()), n = (int) ord.size();
                        setInst (ord[(size_t) ((k + (d == 0 ? -1 : 1) + n) % n)]); return;
                    }
                    juce::String err;
                    if (! p.stepSample (li, d == 0 ? -1 : 1, err) && err.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Korgus Inspiria", err);
                    update();
                };
                addChildComponent (stepB[d]);
            }
            editB.setButtonText (juce::String::fromUTF8 ("✎"));
            editB.setTooltip (T ("Editor samplu – začiatok, koniec, nábeh, doznenie, slučka"));
            editB.onClick = [this]
            {
                if (p.synth.getUserSample (li) == nullptr) return;
                juce::Component::SafePointer<LayerPanel> sp (this);
                auto c = std::make_unique<SampleEditor> (p, li, [sp] { if (sp != nullptr) sp->update(); });
                juce::Component* top = getTopLevelComponent();
                juce::CallOutBox::launchAsynchronously (std::move (c), top != nullptr ? top->getLocalArea (nullptr, editB.getScreenBounds()) : editB.getScreenBounds(), top);
            };
            addChildComponent (editB);
            ratioLabel.setText (T ("FM pomer"), juce::dontSendNotification); ratioLabel.setFont (juce::FontOptions (12.5f));
            ratioLabel.setColour (juce::Label::textColourId, dim.brighter (0.2f)); ratioLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (ratioLabel);
            update();
        }
        std::unique_ptr<Knob> mk (const char* id, const juce::String& title)
        {
            auto k = std::make_unique<Knob> (p.params, pre + id, title);
            if (li == 1) k->setArc (arcB);
            addAndMakeVisible (*k);
            return k;
        }
        bool instMode() const
        {
            return (int) p.params.getRawParameterValue (pre + "wave")->load() == 7 && (int) std::round (p.params.getRawParameterValue (pre + "src")->load()) == 0;
        }
        int curInst() const { return juce::jlimit (0, InstrBank::numInstr - 1, (int) std::round (p.params.getRawParameterValue (pre + "inst")->load())); }
        void setInst (int i)
        {
            if (auto* prm = p.params.getParameter (pre + "inst")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
            p.synth.requestInstr (i);
            update();
        }
        void chooseFile()
        {
            if (auto* ed = findParentComponentOfClass<NovaEditor>())
            {
                juce::Component::SafePointer<LayerPanel> sp (this);
                ed->openWavBrowser (li, [sp] (const juce::File& f, int layer)
                {
                    if (sp == nullptr) return;
                    juce::String err;
                    if (! sp->p.loadSampleFile (layer, f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspiria", err);
                    sp->update();
                });
            }
        }
        void update()
        {
            const int w = (int) p.params.getRawParameterValue (pre + "wave")->load();
            const int srcV = (int) std::round (p.params.getRawParameterValue (pre + "src")->load());
            const bool isSample = w == 7, isUser = isSample && srcV > 0, isWav = isSample && srcV == 1, isSmp = isSample && srcV == 2, isInst = isSample && srcV == 0;
            if (w != lastWave || srcV != lastSrc)
            {
                lastWave = w; lastSrc = srcV;
                const char* ct = "Farba";
                switch (w) { case 3: ct = "Šírka pulzu"; break; case 4: ct = "Farba šumu"; break; case 5: ct = "Jas struny"; break; case 6: ct = "FM index"; break; case 7: ct = "Tvrdosť"; break; case 8: ct = "Focus"; break; case 9: ct = "Pozícia"; break; default: break; }
                color->setTitle (T (ct));
                color->s.setTooltip (w == 9 ? T ("Pozícia vo wavetable – plynulý prechod medzi 64 tvarmi vlny. Hýbe ňou aj Obálka (filtra) a LFO s cieľom Farba.") : juce::String());
                wt.setVisible (w == 9); wtLabel.setVisible (w == 9); disp.setVisible (! isWav);
                row1[3]->setTitle (T (w == 8 ? "Bohatosť" : "Unison")); row1[4]->setTitle (T (w == 8 ? "Rozptyl" : "Rozlad."));
                color->setVisible (w >= 3 && ! isUser);
                noise->setVisible (w != 4 && ! isUser);
                ratio.setVisible (w == 6); ratioLabel.setVisible (w == 6);
                src.setVisible (isSample); load.setVisible (isWav); nameBtn.setVisible (isSmp || isInst); root->setVisible (isWav); loop.setVisible (isWav); smpName.setVisible (isWav);
                stepB[0].setVisible (isUser || isInst); stepB[1].setVisible (isUser || isInst);
                editB.setVisible (isWav);
                nameBtn.setTooltip (T (isInst ? "Klik = zoznam nástrojov" : "Klik = zoznam vzoriek"));
                row1[3]->setEnabled (w <= 3 || w >= 8); row1[4]->setEnabled (w <= 3 || w >= 8);
                resized();
            }
            row3[4]->setEnabled (p.params.getRawParameterValue (pre + "lsync")->load() < 0.5f);   // Rychlost len pri volnom LFO
            auto nm = p.sampleName (li);
            smpName.setText (nm.isNotEmpty() ? nm : T ("(žiadny zvuk)"), juce::dontSendNotification);
            if (isInst)
            {
                const int ci = curInst();
                nm = InstrBank::name (ci, lang() == 1);
                if (! p.synth.instrReady (ci)) nm += T (" (načítavam…)");
            }
            if (nameBtn.getButtonText() != nm + juce::String::fromUTF8 (" ▾")) nameBtn.setButtonText (nm + juce::String::fromUTF8 (" ▾"));
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            sectionLabel (g, { 12, 118, 50, 20 }, "FILTER");
            sectionLabel (g, { 12 + 5 * 46, 118, 130, 20 }, T ("OBÁLKA FILTRA"));
            sectionLabel (g, { 12, 222, 160, 20 }, T ("OBÁLKA HLASITOSTI"));
            sectionLabel (g, { 12 + 4 * 52, 222, 30, 20 }, "LFO");
            g.setColour (line); g.fillRect (12 + 5 * 46 - 2, 146, 1, 66); g.fillRect (12 + 4 * 52 - 2, 250, 1, 66); g.fillRect (12 + 7 * 46 - 2, 42, 1, 66);
            g.fillRect (12, 326, getWidth() - 24, 1);
        }
        void resized() override
        {
            on.setBounds (10, 8, 110, 24);
            wave.setBounds (124, 9, getWidth() - 134, 22);
            for (int i = 0; i < 9; ++i)
            {
                row1[(size_t) i]->setBounds (12 + i * 46, 40, 46, 74);
                row2[(size_t) i]->setBounds (12 + i * 46, 144, 46, 74);
            }
            for (int i = 0; i < 8; ++i) row3[(size_t) i]->setBounds (12 + i * 52, 248, 52, 74);
            ftype.setBounds (62, 117, 108, 21);
            f24.setBounds (174, 117, 60, 22);
            lshape.setBounds (12 + 4 * 52 + 32, 220, 80, 22);
            ltgt.setBounds (12 + 4 * 52 + 116, 220, getWidth() - (12 + 4 * 52 + 116) - 12, 22);
            int x = 12;
            color->setBounds (x, 332, 70, 72); if (color->isVisible()) x += 78;
            noise->setBounds (x, 332, 58, 72); if (noise->isVisible()) x += 64;
            ratioLabel.setBounds (x, 334, 84, 15); ratio.setBounds (x, 354, 84, 24); if (ratio.isVisible()) x += 94;
            wtLabel.setBounds (x, 334, 104, 15); wt.setBounds (x, 354, 104, 24); if (wt.isVisible()) x += 114;
            {
                // zdroj sampla: Klavir | Vzorky | WAV, pod nim ovladanie zvoleneho zdroja
                const int sw = juce::jmin (200, getWidth() - 12 - x - 80);
                const int dx = src.isVisible() ? x + sw + 10 : x;
                disp.setBounds (dx, 334, juce::jmax (10, getWidth() - 12 - dx), 68);
                src.setBounds (x, 336, sw, 24);
                // Vzorky: ◀ nazov ▶
                stepB[0].setBounds (x, 366, 26, 26);
                if (nameBtn.isVisible())
                {
                    nameBtn.setBounds (x + 30, 366, sw - 60, 26);
                    stepB[1].setBounds (x + sw - 26, 366, 26, 26);
                }
                // WAV: Nacitat… ◀ ▶ nazov suboru, vpravo zakladny ton a opakovanie
                load.setBounds (x, 366, 84, 26);
                if (load.isVisible())
                {
                    stepB[0].setBounds (x + 88, 366, 26, 26);
                    stepB[1].setBounds (x + 118, 366, 26, 26);
                }
                editB.setBounds (x + 148, 366, 26, 26);
                root->setBounds (getWidth() - 12 - 64 - 92, 332, 64, 72);
                loop.setBounds (getWidth() - 12 - 88, 366, 88, 22);
                smpName.setBounds (x + 180, 368, juce::jmax (40, root->getX() - 6 - (x + 180)), 22);
            }
        }
        NovaProcessor& p; int li; juce::String pre;
        Toggle on; Seg wave, ftype; Toggle f24; Seg lshape; Combo ltgt, ratio; Seg src; Toggle loop; Combo wt; VoiceDisplay disp; juce::Label wtLabel;
        std::vector<std::unique_ptr<Knob>> row1, row2, row3; std::unique_ptr<Knob> color, root, noise;
        juce::TextButton load, nameBtn, stepB[2], editB; juce::Label smpName, ratioLabel;
        std::unique_ptr<juce::FileChooser> chooser;
        int lastWave = -1, lastSrc = -1;
    };

    // ===================================================================== tube
    struct TubePanel : juce::Component
    {
        TubePanel (NovaProcessor& pr) : p (pr), analog (pr.params, "tube", T ("Analóg")), comp (pr.params, "comp", T ("Kompresia")), compOn (pr.params, "comp_on", T ("Tube komp"), amber.brighter (0.3f))
        {
            addAndMakeVisible (analog); addAndMakeVisible (comp); addAndMakeVisible (compOn);
            analog.setArc (amber); comp.setArc (amber);
            for (auto* b : { &toB, &toA }) { addAndMakeVisible (b); b->setTooltip (T (b == &toB ? "Skopírovať vrstvu A do vrstvy B" : "Skopírovať vrstvu B do vrstvy A")); }
            toB.setButtonText (juce::String::fromUTF8 ("A → B")); toA.setButtonText (juce::String::fromUTF8 ("A ← B"));
            toB.onClick = [this] { p.copyLayer (0, 1); };
            toA.onClick = [this] { p.copyLayer (1, 0); };
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds(), "TUBE", amber.brighter (0.3f));
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText (T ("Teplo a jemné\n„plávanie“ ladenia"), juce::Rectangle<int> (10, 140, getWidth() - 20, 32), juce::Justification::centred, 2);
            g.drawFittedText (T ("Vyrovná hlasité tóny"), juce::Rectangle<int> (10, 302, getWidth() - 20, 18), juce::Justification::centred, 1);
            auto bar = juce::Rectangle<float> (14.0f, 326.0f, (float) getWidth() - 28.0f, 8.0f);
            g.setColour (steel); g.fillRoundedRectangle (bar, 3.0f);
            const float gr = juce::jlimit (0.0f, 20.0f, p.synth.gainReduction());
            g.setColour (amber); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * gr / 20.0f), 3.0f);
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText (T ("Stíšenie ") + juce::String (gr, 1) + " dB", juce::Rectangle<int> (10, 338, getWidth() - 20, 18), juce::Justification::centred);
        }
        void resized() override
        {
            analog.setBounds (20, 36, getWidth() - 40, 100);
            compOn.setBounds (14, 178, getWidth() - 20, 24);
            comp.setBounds (25, 204, getWidth() - 50, 96);
            const int bw = (getWidth() - 34) / 2;
            toB.setBounds (14, 368, bw, 26); toA.setBounds (20 + bw, 368, bw, 26);
        }
        NovaProcessor& p; Knob analog, comp; Toggle compOn; juce::TextButton toB, toA;
    };

    // ===================================================================== graf EQ (tahanie bodov)
    struct EqGraph : juce::Component, private juce::Timer
    {
        EqGraph (NovaProcessor& pr) : p (pr), fft (12) { spec.assign (2048, -120.0f); }
        ~EqGraph() override { p.synth.analyzer.tap.store (-1); }
        // spektrum: zvoleny bod v zvuku sa zbiera len ked je graf viditelny
        void visibilityChanged() override { updateTap(); }
        void parentHierarchyChanged() override { updateTap(); }
        void updateTap()
        {
            const bool on = isShowing();
            p.synth.analyzer.tap.store (on ? tapIdx : -1);
            if (on) startTimerHz (30); else { stopTimer(); std::fill (spec.begin(), spec.end(), -120.0f); }
        }
        void timerCallback() override
        {
            auto& an = p.synth.analyzer;
            if (! isShowing()) { updateTap(); return; }
            constexpr int N = 4096;
            const unsigned w = an.w.load (std::memory_order_acquire);
            if (w == lastW) { for (auto& v : spec) v -= 1.5f; repaint(); return; }
            lastW = w;
            std::vector<float> d ((size_t) N * 2, 0.0f);
            for (int i = 0; i < N; ++i)
            {
                const float x = an.buf[(w - (unsigned) N + (unsigned) i) & (Analyzer::N - 1)];
                d[(size_t) i] = x * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1)));
            }
            fft.performFrequencyOnlyForwardTransform (d.data());
            for (int i = 0; i < N / 2; ++i)
            {
                const float db = juce::Decibels::gainToDecibels (d[(size_t) i] / (float) (N / 4), -120.0f);
                spec[(size_t) i] = db > spec[(size_t) i] ? db : std::max (db, spec[(size_t) i] - 1.5f);
            }
            sr = an.rate.load();
            repaint();
        }
        // pasmo v grafe: typ (0 basy shelf, 1 spicka, 2 vysky shelf, 3 orezanie basov, 4 orezanie vysok), indexy parametrov (-1 = pevna hodnota)
        struct Band { int type, f, g, q; float fFix, qFix, fMin, fMax, fDef; };
        void setTarget (const juce::String& prefix, juce::Colour c, int tap)
        {
            col = c; tapIdx = tap; lastW = 0;
            std::fill (spec.begin(), spec.end(), -120.0f);
            updateTap();
            const bool snd = prefix == "eq_";
            bands.clear();
            if (snd)
            {   // 8 pasiem celeho zvuku
                ids = { "eq_hpf", "eq_low", "eq_lowf", "eq_mid", "eq_midf", "eq_q1", "eq_p2g", "eq_p2f", "eq_p2q", "eq_p3g", "eq_p3f", "eq_p3q", "eq_p4g", "eq_p4f", "eq_p4q", "eq_high", "eq_highf", "eq_lpf" };
                bands = { { 3, 0, -1, -1, 0, 0.707f, 20, 1000, 20 }, { 0, 2, 1, -1, 0, 0.7f, 40, 600, 150 },
                          { 1, 4, 3, 5, 0, 0, 40, 16000, 1000 }, { 1, 7, 6, 8, 0, 0, 40, 16000, 300 }, { 1, 10, 9, 11, 0, 0, 40, 16000, 3000 }, { 1, 13, 12, 14, 0, 0, 40, 16000, 6000 },
                          { 2, 16, 15, -1, 0, 0.7f, 1000, 16000, 2500 }, { 4, 17, -1, -1, 0, 0.707f, 1000, 20000, 20000 } };
            }
            else
            {
                ids = { prefix + "eql", prefix + "eqm", prefix + "eqmf", prefix + "eqh" };
                bands = { { 0, -1, 0, -1, 150, 0.7f, 150, 150, 150 }, { 1, 2, 1, -1, 0, 0.8f, 200, 8000, 1000 }, { 2, -1, 3, -1, 2500, 0.7f, 2500, 2500, 2500 } };
            }
            atts.clear(); val.assign ((size_t) ids.size(), 0.0f);
            for (int i = 0; i < ids.size(); ++i)
            {
                atts.push_back (std::make_unique<juce::ParameterAttachment> (*p.params.getParameter (ids[i]), [this, i] (float v) { val[(size_t) i] = v; repaint(); }, &p.undo));
                atts.back()->sendInitialUpdate();
            }
            repaint();
        }
        float bf (const Band& b) const { return b.f >= 0 ? val[(size_t) b.f] : b.fFix; }
        float bg (const Band& b) const { return b.g >= 0 ? val[(size_t) b.g] : 0.0f; }
        float bq (const Band& b) const { return b.q >= 0 ? val[(size_t) b.q] : b.qFix; }
        bool bandActive (const Band& b) const
        {
            if (b.type == 3) return bf (b) > 21.0f;
            if (b.type == 4) return bf (b) < 19900.0f;
            return bg (b) != 0.0f;
        }
        static constexpr float fLo = 20.0f, fHi = 20000.0f, dbR = 18.0f;
        float xOf (float hz) const { return (float) getWidth() * std::log (hz / fLo) / std::log (fHi / fLo); }
        float hzOf (float x) const { return fLo * std::pow (fHi / fLo, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
        float yOf (float db) const { return graphH() * 0.5f - db / dbR * (graphH() * 0.5f - 6.0f); }
        float dbOf (float y) const { return -(y - graphH() * 0.5f) / (graphH() * 0.5f - 6.0f) * dbR; }
        float graphH() const { return (float) getHeight() - 18.0f; }
        juce::Point<float> pt (int k) const { const auto& b = bands[(size_t) k]; return { xOf (bf (b)), yOf (bg (b)) }; }
        void paint (juce::Graphics& g) override
        {
            const auto gr = getLocalBounds().toFloat().withHeight (graphH());
            g.setColour (juce::Colour (0xff1a2029)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
            g.setColour (line);
            for (float db : { -12.0f, 12.0f }) g.drawHorizontalLine (juce::roundToInt (yOf (db)), 0, (float) getWidth());
            g.setColour (line.brighter (0.15f)); g.drawHorizontalLine (juce::roundToInt (yOf (0)), 0, (float) getWidth());
            g.setFont (juce::FontOptions (11.0f));
            for (auto f : { 100.0f, 1000.0f, 10000.0f })
            {
                const int x = juce::roundToInt (xOf (f));
                g.setColour (line); g.drawVerticalLine (x, 0, (float) getHeight());
                g.setColour (dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), x + 4, (int) gr.getBottom() + 2, 40, 14, juce::Justification::centredLeft);
            }
            g.setColour (dim); g.drawText ("+12 dB", 4, juce::roundToInt (yOf (12)) - 14, 50, 13, juce::Justification::centredLeft);
            g.drawText ("-12 dB", 4, juce::roundToInt (yOf (-12)) + 1, 50, 13, juce::Justification::centredLeft);
            // spektrum v pozadi (0 dB hore, -84 dB dole)
            {
                juce::Path sp; const float H = graphH();
                sp.startNewSubPath (0, H);
                const int bins = (int) spec.size();
                for (int x = 0; x <= getWidth(); x += 2)
                {
                    const float f0 = hzOf ((float) x), f1 = hzOf ((float) x + 2.0f);
                    int b0 = juce::jlimit (1, bins - 1, (int) (f0 / sr * 4096.0f)), b1 = juce::jlimit (b0, bins - 1, (int) (f1 / sr * 4096.0f));
                    float m = -120.0f; for (int b = b0; b <= b1; ++b) m = std::max (m, spec[(size_t) b]);
                    m += 4.5f * std::log2 (juce::jmax (20.0f, f0) / 1000.0f);   // sklon -4.5 dB/okt (ako pocuje ucho)
                    const float y = juce::jlimit (0.0f, H, H * (-m) / 84.0f);
                    sp.lineTo ((float) x, y);
                }
                sp.lineTo ((float) getWidth(), H); sp.closeSubPath();
                const juce::Colour sc (0xff4f7d8c);
                g.setGradientFill (juce::ColourGradient (sc.withAlpha (0.55f), 0, 0, sc.withAlpha (0.12f), 0, H, false));
                g.fillPath (sp);
                g.setColour (sc.brighter (0.2f).withAlpha (0.6f)); g.strokePath (sp, juce::PathStrokeType (1.0f));
            }
            // krivka (sucin vsetkych pasiem)
            std::vector<Biquad> bq2;
            for (auto& b : bands) { Biquad q; q.set (b.type, bf (b), bq (b), bg (b), 48000); bq2.push_back (q); }
            juce::Path path;
            for (int x = 0; x <= getWidth(); x += 2)
            {
                const double w = juce::MathConstants<double>::twoPi * hzOf ((float) x) / 48000.0;
                const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
                double mag = 1;
                for (size_t k = 0; k < bq2.size(); ++k)
                {
                    if (! bandActive (bands[k])) continue;
                    const auto& q = bq2[k];
                    mag *= std::abs (((double) q.b0 + (double) q.b1 * z1 + (double) q.b2 * z2) / (1.0 + (double) q.a1 * z1 + (double) q.a2 * z2));
                }
                const float y = juce::jlimit (0.0f, graphH(), yOf ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag)))));
                if (x == 0) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
            }
            g.setColour (enabled ? col : col.withAlpha (0.35f)); g.strokePath (path, juce::PathStrokeType (2.5f));
            // tahane pasmo: jeho vlastna krivka zvyraznena (vidno sirku)
            if (drag >= 0 && drag < (int) bands.size())
            {
                const auto& q = bq2[(size_t) drag]; juce::Path one; const float y0 = yOf (0);
                one.startNewSubPath (0, y0);
                for (int x = 0; x <= getWidth(); x += 2)
                {
                    const double w = juce::MathConstants<double>::twoPi * hzOf ((float) x) / 48000.0;
                    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
                    const double mag = std::abs (((double) q.b0 + (double) q.b1 * z1 + (double) q.b2 * z2) / (1.0 + (double) q.a1 * z1 + (double) q.a2 * z2));
                    one.lineTo ((float) x, juce::jlimit (0.0f, graphH(), yOf ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag))))));
                }
                one.lineTo ((float) getWidth(), y0); one.closeSubPath();
                g.setColour (col.withAlpha (0.18f)); g.fillPath (one);
            }
            // body pasiem (orezania ako stvorceky na nulovej ciare), cisla pasiem pri 8-pasmovom EQ
            const bool many = bands.size() > 3;
            for (int k = 0; k < (int) bands.size(); ++k)
            {
                const auto c = pt (k); const auto& b = bands[(size_t) k];
                const bool act = bandActive (b);
                g.setColour (k == drag ? text : (act ? col : col.withAlpha (0.45f)));
                const float r = many ? 7.0f : 8.0f;
                if (b.type >= 3) g.fillRect (c.x - r + 1, c.y - r + 1, 2 * r - 2, 2 * r - 2); else g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
                if (many) { g.setColour (juce::Colour (0xff1a2029)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold)); g.drawText (juce::String (k + 1), (int) (c.x - r), (int) (c.y - r), (int) (2 * r), (int) (2 * r), juce::Justification::centred); }
            }
            if (drag >= 0 && drag < (int) bands.size())
            {   // hodnoty prave tahaneho bodu
                const auto& b = bands[(size_t) drag];
                const float f = bf (b);
                juce::String t = (f >= 1000 ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
                if (b.g >= 0) t += "   " + juce::String (bg (b), 1) + " dB";
                if (b.q >= 0) t += "   Q " + juce::String (bq (b), 1);
                g.setColour (text); g.setFont (juce::FontOptions (11.5f)); g.drawText (t, getWidth() - 200, 4, 194, 14, juce::Justification::centredRight);
            }
        }
        int hit (juce::Point<float> m) const
        {
            int best = -1; float bd = 16.0f;
            for (int k = 0; k < (int) bands.size(); ++k) { const float d = pt (k).getDistanceFrom (m); if (d < bd) { bd = d; best = k; } }
            return best;
        }
        void gesture (int k, bool begin)
        {
            const auto& b = bands[(size_t) k];
            for (int id : { b.f, b.g }) if (id >= 0 && ! (b.f >= 0 && b.fMin == b.fMax && id == b.f)) { if (begin) atts[(size_t) id]->beginGesture(); else atts[(size_t) id]->endGesture(); }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            drag = hit (e.position); qDrag = false;
            if (drag < 0) { repaint(); return; }
            // ako FabFilter: pravym tlacidlom / Shift / Alt + tahanie hore-dole = sirka pasma (uzsie / sirsie)
            const auto& b = bands[(size_t) drag];
            if (b.q >= 0 && (e.mods.isRightButtonDown() || e.mods.isShiftDown() || e.mods.isAltDown()))
            { qDrag = true; qStart = val[(size_t) b.q]; yStart = e.position.y; atts[(size_t) b.q]->beginGesture(); }
            else gesture (drag, true);
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (drag < 0) return;
            const auto& b = bands[(size_t) drag];
            if (qDrag) { atts[(size_t) b.q]->setValueAsPartOfGesture (juce::jlimit (0.3f, 8.0f, qStart * std::pow (2.0f, (yStart - e.position.y) / 40.0f))); return; }
            if (b.g >= 0) atts[(size_t) b.g]->setValueAsPartOfGesture (juce::jlimit (-15.0f, 15.0f, dbOf (e.position.y)));
            if (b.f >= 0 && b.fMin < b.fMax) atts[(size_t) b.f]->setValueAsPartOfGesture (juce::jlimit (b.fMin, b.fMax, hzOf (e.position.x)));
        }
        void mouseUp (const juce::MouseEvent&) override { if (drag < 0) return; if (qDrag) atts[(size_t) bands[(size_t) drag].q]->endGesture(); else gesture (drag, false); drag = -1; qDrag = false; repaint(); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {   // koliesko nad bodom = sirka pasma (Q)
            const int k = hit (e.position);
            if (k < 0 || bands[(size_t) k].q < 0) return;
            const int id = bands[(size_t) k].q;
            atts[(size_t) id]->setValueAsCompleteGesture (juce::jlimit (0.3f, 8.0f, val[(size_t) id] * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f)));
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int k = hit (e.position);
            if (k < 0) return;
            const auto& b = bands[(size_t) k];
            if (b.g >= 0) atts[(size_t) b.g]->setValueAsCompleteGesture (0);
            if (b.f >= 0 && b.fMin < b.fMax) atts[(size_t) b.f]->setValueAsCompleteGesture (b.fDef);
            if (b.q >= 0) atts[(size_t) b.q]->setValueAsCompleteGesture (k == 2 && bands.size() > 3 ? 0.8f : 1.0f);
        }
        void flat()
        {
            p.undo.beginNewTransaction (T ("Plochý EQ"));
            for (int k = 0; k < (int) bands.size(); ++k)
            {
                const auto& b = bands[(size_t) k];
                if (b.g >= 0) atts[(size_t) b.g]->setValueAsCompleteGesture (0);
                if (b.f >= 0 && b.fMin < b.fMax) atts[(size_t) b.f]->setValueAsCompleteGesture (b.fDef);
            }
        }
        NovaProcessor& p; juce::StringArray ids; juce::Colour col = arc;
        std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
        std::vector<Band> bands; std::vector<float> val; int drag = -1; bool qDrag = false; float qStart = 1, yStart = 0;
        bool enabled = true; int tapIdx = 0; unsigned lastW = 0; float sr = 48000;
        juce::dsp::FFT fft; std::vector<float> spec;
    };

    // ===================================================================== efekty + EQ
    // ciele EQ: 0 vrstva A, 1 vrstva B, 2 cely zvuk, 3 delay, 4 dozvuk
    constexpr int NEQ = 6;
    const char* eqPrefix (int t) { static const char* p[NEQ] = { "a_", "b_", "eq_", "dl_", "rv_", "vc_" }; return p[t]; }
    int eqTap (int t) { static const int tp[NEQ] = { 0, 1, 2, 4, 3, 2 }; return tp[t]; }
    juce::Colour eqColour (int t) { return t == 0 ? arc : (t == 1 ? arcB : (t == 2 ? text.darker (0.15f) : (t == 3 ? juce::Colour (0xff7fa89a) : (t == 5 ? juce::Colour (0xffd08a6a) : amber.brighter (0.25f))))); }

    struct FxPanel : juce::Component
    {
        FxPanel (NovaProcessor& pr) : p (pr),
            drive (pr.params, "drive_on"), chorus (pr.params, "ch_on"), delay (pr.params, "dl_on"), reverb (pr.params, "rv_on"), pp (pr.params, "dl_pp"),
            sync (pr.params, "dl_sync", juce::StringArray::fromTokens (T ("Voľne|1/4|1/8.|1/8|1/8T|1/16"), "|", "")),
            chType (pr.params, "ch_type", juce::StringArray::fromTokens ("Chorus|Ensemble|Flanger|Phaser|Tremolo", "|", "")), graph (pr)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &drive, &chorus, &delay, &reverb, &pp, &sync, &chType }) addAndMakeVisible (c);
            chType.setTips ({ T ("Chorus = širší zvuk"), T ("Ensemble = sláčikový ensemble"), T ("Flanger = „lietadlo“"), T ("Phaser = „lietadlo“, jemnejšie"), T ("Tremolo = kolísanie hlasitosti") });
            sync.setTips ({ T ("Voľne = čas v ms, ostatné = podľa tempa (1/8. = osminka s bodkou, T = triola)") });
            chorus.setButtonText ({}); chorus.setTooltip (T ("Modulácia zap / vyp"));
            auto add = [this] (std::vector<std::unique_ptr<Knob>>& v, const juce::String& id, const char* t) { v.push_back (std::make_unique<Knob> (p.params, id, T (t))); addAndMakeVisible (*v.back()); };
            add (fx, "drive", "Sila");
            add (fx, "ch_rate", "Rýchlosť"); add (fx, "ch_depth", "Hĺbka"); add (fx, "ch_mix", "Mix");
            add (fx, "dl_time", "Čas"); add (fx, "dl_fb", "Opakov."); add (fx, "dl_tone", "Farba"); add (fx, "dl_mix", "Mix");
            add (fx, "rv_size", "Veľkosť"); add (fx, "rv_damp", "Tlmenie"); add (fx, "rv_mix", "Mix");
            add (fx, "ch_fb", "Sp. väzba"); add (fx, "rv_pre", "Predelay");   // [11], [12]
            fx[12]->s.setTooltip (T ("Oneskorenie dozvuku – oddelí čistý zvuk od dozvuku, znie zreteľnejšie"));
            // EQ: 5 cielov x 4 gombiky (A, B, Zvuk, Delay, Dozvuk)
            for (int t = 0; t < NEQ; ++t)
            {
                const juce::String px = eqPrefix (t);
                if (t == 2) { add (eq, "eq_low", ""); add (eq, "eq_mid", "Stredy"); add (eq, "eq_midf", "Stredy Hz"); add (eq, "eq_high", "Výšky"); }
                else { add (eq, px + "eql", ""); add (eq, px + "eqm", "Stredy"); add (eq, px + "eqmf", "Stredy Hz"); add (eq, px + "eqh", "Výšky"); }
                for (int k = 0; k < 4; ++k) eq[(size_t) (t * 4 + k)]->setArc (eqColour (t));
            }
            for (int i = 0; i < (int) eq.size(); i += 4) eq[(size_t) i]->label.setText (T ("Basy", "Low"), juce::dontSendNotification);
            const char* tn[] = { "Vrstva A", "Vrstva B", "Zvuk", "Delay", "Dozvuk", "Vocoder" };
            const char* tt[] = { "EQ len pre vrstvu A", "EQ len pre vrstvu B", "EQ celého zvuku", "EQ len pre opakovania delayu (napr. ubrať basy a výšky, aby delay nezahlcoval)", "EQ len pre dozvuk (napr. ubrať basy, aby dozvuk nehučal)", "EQ len pre vocoder (napr. ubrať výšky, ak sú sykavky ostré)" };
            for (int t = 0; t < NEQ; ++t)
            {
                auto& b = targets[t];
                b.setButtonText (T (tn[t])); b.setTooltip (T (tt[t]));
                b.onClick = [this, t] { setTarget (t); };
                addAndMakeVisible (b);
            }
            for (int t = 0; t < NEQ; ++t)
            {
                const juce::String id = t == 2 ? juce::String ("eq_on") : juce::String (eqPrefix (t)) + "eqon";
                eqOn[t] = std::make_unique<Toggle> (p.params, id, T ("Zapnutý"));
                eqOn[t]->setTooltip (T ("Vypnúť / zapnúť tento EQ (na porovnanie)"));
                eqOn[t]->onStateChange = [this] { syncEnabled(); };
                addAndMakeVisible (*eqOn[t]);
            }
            excK = std::make_unique<Knob> (p.params, "exc", T ("Lesk")); excK->s.setTooltip (T ("Doplní jemné výšky (vzduch) – hlavne nahrávkam s orezanými výškami, napr. klavíru")); addChildComponent (*excK);
            flatBtn.setButtonText (T ("Plochý")); flatBtn.setTooltip (T ("Vynulovať tento EQ"));
            flatBtn.onClick = [this] { graph.flat(); };
            addAndMakeVisible (flatBtn); addAndMakeVisible (graph);
            help.setText (T ("Ťahaj body: hore / dole = hlasnejšie / tichšie, vľavo / vpravo = výška. Pravé tlačidlo (alebo Shift) + ťahanie hore / dole = užšie / širšie pásmo, rovnako koliesko. Dvojklik = vynulovať."), juce::dontSendNotification);
            help.setFont (juce::FontOptions (12.0f)); help.setColour (juce::Label::textColourId, dim); help.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (help);
            for (auto* b : { &tabFx, &tabEq, &tabGate, &tabMod }) addAndMakeVisible (b);
            tabFx.setButtonText (T ("EFEKTY")); tabEq.setButtonText ("EQ"); tabGate.setButtonText ("GATER"); tabMod.setButtonText ("MOD");
            tabGate.setTooltip (T ("Rytmický gater: seká zvuk do rytmu podľa tempa (trance, pop pady)"));
            tabMod.setTooltip (T ("Modulačná matica: LFO, obálka, sila úderu… môže hýbať ľubovoľným cieľom"));
            tabFx.onClick = [this] { showTab (0); }; tabEq.onClick = [this] { showTab (1); };
            tabGate.onClick = [this] { showTab (2); }; tabMod.onClick = [this] { showTab (3); };
            // vocoder
            addAndMakeVisible (tabVoc); tabVoc.setButtonText ("VOCODER"); tabVoc.onClick = [this] { showTab (4); };
            tabVoc.setTooltip (T ("Vocoder: Inspiria spieva slovami zo spevovej stopy"));
            vcOn = std::make_unique<Toggle> (p.params, "vc_on", T ("VOCODER"), amber.brighter (0.3f));
            addAndMakeVisible (*vcOn);
            {
                const char* ids[6] = { "vc_bands", "vc_formant", "vc_clarity", "vc_release", "vc_dry", "vc_mix" };
                const char* nm[6] = { "Pásma", "Formant", "Zrozumiteľnosť", "Dozvuk", "Hlas", "Mix" };
                const char* tip[6] = { "Menej pásiem = klasický robot, viac = zrozumiteľnejšie",
                                       "Posun farby hlasu (nižšie = mohutnejší, vyššie = detský / robotický)",
                                       "Spoluhlásky S, T, K z pôvodného hlasu – aby bolo rozumieť slovám",
                                       "Ako plynulo doznieva každé slovo",
                                       "Trochu pôvodného hlasu pod vocoderom (človek + robot)",
                                       "Pomer syntezátor / vocoder: vľavo čistý syntezátor, vpravo len vocoder" };
                for (int k = 0; k < 6; ++k)
                {
                    vcK[k] = std::make_unique<Knob> (p.params, ids[k], T (nm[k]));
                    vcK[k]->setArc (amber); vcK[k]->s.setTooltip (T (tip[k]));
                    addAndMakeVisible (*vcK[k]);
                }
            }
            vcHelp.setFont (juce::FontOptions (12.0f)); vcHelp.setColour (juce::Label::textColourId, dim);
            vcHelp.setJustificationType (juce::Justification::topLeft);
            vcHelp.setText (T ("Vyber stopu so spevom a hraj akordy – Inspiria zaspieva slovami zo spevu.\nVyskúšaj zvuky „Vocoder…“ v zozname zvukov."), juce::dontSendNotification);
            addAndMakeVisible (vcHelp);
            addAndMakeVisible (vcMeter);
            vcSrc.setTextWhenNothingSelected (T ("Hlas zo stopy…"));
            vcSrc.setTextWhenNoChoicesAvailable (T ("V piesni nie je audio stopa so spevom"));
            vcSrc.setTooltip (T ("Vyber stopu so spevom – Inspiria bude spievať jej slovami"));
            vcSrc.setWantsKeyboardFocus (false);
            vcSrc.onChange = [this]
            {
                const int id = vcSrc.getSelectedId();
                if (id <= 0 || vcSrcSync) return;
                if (auto* prm = p.params.getParameter ("vc_src")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) id));
                if (auto* on = p.params.getParameter ("vc_on")) on->setValueNotifyingHost (1.0f);
            };
            addAndMakeVisible (vcSrc);
            vcMeter.onTick = [this] { refreshVocTracks(); };
            // gater
            gtOn = std::make_unique<Toggle> (p.params, "gt_on", T ("GATER"), amber.brighter (0.3f));
            gtRate = std::make_unique<Seg> (p.params, "gt_rate", juce::StringArray::fromTokens ("1/4|1/8|1/8T|1/16|1/32", "|", ""), amber);
            gtDepth = std::make_unique<Knob> (p.params, "gt_depth", T ("Hĺbka"));
            gtSmooth = std::make_unique<Knob> (p.params, "gt_smooth", T ("Hladkosť"));
            gtSwing = std::make_unique<Knob> (p.params, "gt_swing", "Swing");
            gtDepth->setArc (amber); gtSmooth->setArc (amber); gtSwing->setArc (amber);
            for (auto* c : std::initializer_list<juce::Component*> { gtOn.get(), gtRate.get(), gtDepth.get(), gtSmooth.get(), gtSwing.get(), (juce::Component*) &gtPreset }) addAndMakeVisible (c);
            gtPreset.setTextWhenNothingSelected (T ("Rytmus…"));
            for (int k = 0; k < korgus::GatePatterns::count; ++k) gtPreset.addItem (lang() == 1 ? juce::String (korgus::GatePatterns::nameEn (k)) : juce::String::fromUTF8 (korgus::GatePatterns::nameSk (k)), k + 1);
            gtPreset.setTooltip (T ("Hotové rytmy – nastavia kroky vzoru"));
            gtPreset.setWantsKeyboardFocus (false);
            gtPreset.onChange = [this]
            {
                const int k = gtPreset.getSelectedId() - 1;
                if (k < 0) return;
                const char* b = korgus::GatePatterns::bits (k);
                for (int i = 0; i < 16; ++i)
                    if (auto* prm = p.params.getParameter ("gt_s" + juce::String (i))) prm->setValueNotifyingHost (b[i] == '1' ? 1.0f : 0.0f);
                if (auto* on = p.params.getParameter ("gt_on")) on->setValueNotifyingHost (1.0f);
            };
            gtRate->setTips ({ T ("Dĺžka jedného kroku vzoru") });
            for (int k = 0; k < 16; ++k)
            {
                auto& b = steps[k];
                b.setClickingTogglesState (true);
                b.setColour (juce::TextButton::buttonOnColourId, amber);
                b.setTooltip (T ("Krok vzoru – klik zapne / vypne"));
                stepAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.params, "gt_s" + juce::String (k), b);
                addAndMakeVisible (b);
            }
            gtHelp.setText (T ("Zapni GATER a klikaj na kroky: zlaté = zvuk hrá, tmavé = ticho.\nNajlepšie znie s padmi a dlhými tónmi."), juce::dontSendNotification);
            gtHelp.setFont (juce::FontOptions (12.0f)); gtHelp.setColour (juce::Label::textColourId, dim); gtHelp.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (gtHelp);
            // modulacna matica
            for (int k = 0; k < 4; ++k)
            {
                const juce::String m = "mm" + juce::String (k + 1) + "_";
                mSrc[k] = std::make_unique<Combo> (p.params, m + "src");
                mDst[k] = std::make_unique<Combo> (p.params, m + "dst");
                mAmt[k] = std::make_unique<Knob> (p.params, m + "amt", T ("Množstvo"));
                mSrc[k]->setTooltip (T ("Zdroj modulácie")); mDst[k]->setTooltip (T ("Čím sa bude hýbať"));
                mAmt[k]->setArc (amber);
                for (auto* c : std::initializer_list<juce::Component*> { mSrc[k].get(), mDst[k].get(), mAmt[k].get() }) addAndMakeVisible (c);
            }
            auto mkSlider = [this] (juce::Slider& s, juce::Label& l, const char* id, const char* t)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                addAndMakeVisible (s); atts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params, id, s));
                l.setText (T (t), juce::dontSendNotification); l.setFont (juce::FontOptions (12.5f)); l.setColour (juce::Label::textColourId, dim.brighter (0.2f));
                addAndMakeVisible (l);
                s.setDoubleClickReturnValue (true, findSpec (id)->def);
            };
            mkSlider (width, widthL, "fx_width", "Šírka"); mkSlider (punch, punchL, "fx_punch", "Úder");
            setTarget (0);
            showTab (0);
        }
        void setTarget (int t)
        {
            target = t;
            for (int i = 0; i < NEQ; ++i) targets[i].setToggleState (i == t, juce::dontSendNotification);
            graph.setTarget (eqPrefix (t), eqColour (t), eqTap (t));
            showTab (tab);
        }
        void tick()   // spatna vazba ma zmysel len pri flangeri a phaseri
        {
            const int ty = (int) p.params.getRawParameterValue ("ch_type")->load();
            const bool fbOn = ty == 2 || ty == 3;
            if (fx[11]->isEnabled() != fbOn) fx[11]->setEnabled (fbOn);
        }
        void syncEnabled()
        {
            const bool e = eqOn[target]->getToggleState();
            if (e != graph.enabled) { graph.enabled = e; graph.repaint(); }
        }
        void showTab (int t)
        {
            tab = t;
            const bool e = t == 1, fxv = t == 0, gv = t == 2, mv = t == 3, vv = t == 4;
            tabVoc.setToggleState (vv, juce::dontSendNotification);
            vcOn->setVisible (vv); vcHelp.setVisible (vv); vcMeter.setVisible (vv); vcSrc.setVisible (vv); for (auto& k : vcK) k->setVisible (vv);
            eqShown = e;
            tabFx.setToggleState (fxv, juce::dontSendNotification); tabEq.setToggleState (e, juce::dontSendNotification);
            tabGate.setToggleState (gv, juce::dontSendNotification); tabMod.setToggleState (mv, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &drive, &chorus, &delay, &reverb, &pp, &sync, &chType }) c->setVisible (fxv);
            for (auto& k : fx) k->setVisible (fxv);
            for (auto* c : std::initializer_list<juce::Component*> { gtOn.get(), gtRate.get(), gtDepth.get(), gtSmooth.get(), gtSwing.get(), (juce::Component*) &gtHelp, (juce::Component*) &gtPreset }) c->setVisible (gv);
            for (auto& b : steps) b.setVisible (gv);
            for (int k = 0; k < 4; ++k) { mSrc[k]->setVisible (mv); mDst[k]->setVisible (mv); mAmt[k]->setVisible (mv); }
            for (int i = 0; i < (int) eq.size(); ++i) eq[(size_t) i]->setVisible (e && i / 4 == target);
            for (auto& b : targets) b.setVisible (e);
            flatBtn.setVisible (e); graph.setVisible (e); help.setVisible (e); if (excK) excK->setVisible (e && target == 2);
            for (int t = 0; t < NEQ; ++t) eqOn[t]->setVisible (e && t == target);
            syncEnabled();
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            if (tab == 0) { g.setColour (line); g.fillRect (276, 44, 1, 196); }
            if (tab == 3)
            {
                g.setColour (dim); g.setFont (juce::FontOptions (12.0f));
                for (int k = 0; k < 4; ++k) g.drawText (juce::String (k + 1), 12, 46 + k * 48, 16, 24, juce::Justification::centredLeft);
            }
        }
        void resized() override
        {
            tabFx.setBounds (10, 8, 62, 22); tabEq.setBounds (74, 8, 34, 22); tabGate.setBounds (110, 8, 52, 22); tabMod.setBounds (164, 8, 40, 22); tabVoc.setBounds (206, 8, 70, 22);
            widthL.setBounds (284, 8, 38, 22); width.setBounds (322, 8, 70, 22);
            punchL.setBounds (398, 8, 38, 22); punch.setBounds (436, 8, getWidth() - 446, 22);
            // vocoder
            vcOn->setBounds (12, 40, 110, 22); vcSrc.setBounds (130, 40, 200, 22); vcMeter.setBounds (340, 44, getWidth() - 352, 14);
            vcHelp.setBounds (12, 70, getWidth() - 24, 46);
            {
                const int kw = (getWidth() - 24) / 6;
                for (int k = 0; k < 6; ++k) vcK[k]->setBounds (12 + k * kw, 122, kw, 80);
            }
            // gater
            gtOn->setBounds (12, 40, 92, 22); gtRate->setBounds (108, 40, 150, 22); gtPreset.setBounds (262, 40, getWidth() - 192 - 268, 22);
            gtDepth->setBounds (getWidth() - 192, 36, 58, 72); gtSmooth->setBounds (getWidth() - 132, 36, 58, 72); gtSwing->setBounds (getWidth() - 72, 36, 58, 72);
            gtHelp.setBounds (12, 70, getWidth() - 210, 40);
            {
                const int sw = (getWidth() - 24 - 15 * 4 - 3 * 6) / 16;
                for (int k = 0; k < 16; ++k) steps[k].setBounds (12 + k * (sw + 4) + (k / 4) * 6, 128, sw, 70);
            }
            // modulacna matica
            for (int k = 0; k < 4; ++k)
            {
                const int y = 44 + k * 48;
                mSrc[k]->setBounds (30, y + 2, 170, 24);
                mDst[k]->setBounds (210, y + 2, 150, 24);
                mAmt[k]->setBounds (getWidth() - 74, y - 4, 60, 46);
            }
            drive.setBounds (12, 40, 100, 22);
            fx[0]->setBounds (12, 62, 58, 72);
            delay.setBounds (12, 142, 90, 22); pp.setBounds (184, 142, 90, 22); sync.setBounds (12, 244, 256, 20);
            for (int i = 0; i < 4; ++i) fx[(size_t) (4 + i)]->setBounds (12 + i * 64, 166, 60, 72);
            const int x2 = 286;
            chorus.setBounds (x2, 40, 24, 22); chType.setBounds (x2 + 26, 40, 230 - 26 + 2, 22);
            const int chk[4] = { 1, 2, 11, 3 }, rvk[4] = { 8, 12, 9, 10 };
            for (int i = 0; i < 4; ++i) fx[(size_t) chk[i]]->setBounds (x2 + i * 58, 64, 56, 70);
            reverb.setBounds (x2, 142, 100, 22);
            for (int i = 0; i < 4; ++i) fx[(size_t) rvk[i]]->setBounds (x2 + i * 58, 166, 56, 72);
            // EQ
            for (int t = 0; t < NEQ; ++t) targets[t].setBounds (12 + t * 64, 38, 61, 24);
            flatBtn.setBounds (getWidth() - 12 - 70, 38, 70, 24);
            // prepinac Zapnuty pod tlacidlom Plochy (v jednom riadku s tlacidlami vrstiev sa neprekryva)
            for (auto& tg : eqOn) tg->setBounds (getWidth() - 12 - 96, 66, 96, 22);
            for (int i = 0; i < (int) eq.size(); ++i) eq[(size_t) i]->setBounds (12 + (i % 4) * 62, 66, 58, 70);
            if (excK) excK->setBounds (getWidth() - 12 - 62, 76, 62, 66);
            help.setBounds (270, 92, getWidth() - 282 - 62, 52);
            graph.setBounds (12, 140, getWidth() - 24, getHeight() - 150);
        }
        NovaProcessor& p;
        Toggle drive, chorus, delay, reverb, pp; Seg sync, chType;
        std::vector<std::unique_ptr<Knob>> fx, eq;
        std::unique_ptr<Knob> excK;
        juce::TextButton tabFx, tabEq, tabGate, tabMod, targets[NEQ], flatBtn; juce::Slider width, punch; juce::Label widthL, punchL, help, gtHelp;
        std::unique_ptr<Toggle> gtOn; std::unique_ptr<Seg> gtRate; std::unique_ptr<Combo> mSrc[4], mDst[4]; std::unique_ptr<Knob> gtDepth, gtSmooth, gtSwing, mAmt[4]; juce::ComboBox gtPreset;
        juce::TextButton tabVoc; std::unique_ptr<Toggle> vcOn; std::unique_ptr<Knob> vcK[6]; juce::Label vcHelp;
        juce::ComboBox vcSrc; bool vcSrcSync = false; juce::String vcSrcList;
        // zoznam spevovych stop zapisuje Korgus Sequencer do docasneho suboru
        void refreshVocTracks()
        {
            const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("korgus_vokoder_stopy.txt");
            const auto txt = f.existsAsFile() ? f.loadFileAsString() : juce::String();
            const int cur = (int) std::round (p.params.getRawParameterValue ("vc_src")->load());
            if (txt == vcSrcList && vcSrc.getSelectedId() == cur) return;
            vcSrcList = txt;
            juce::ScopedValueSetter<bool> g (vcSrcSync, true);
            vcSrc.clear (juce::dontSendNotification);
            auto lines = juce::StringArray::fromLines (txt); lines.removeEmptyStrings();
            for (int i = 0; i < lines.size() && i < 32; ++i) vcSrc.addItem (lines[i], i + 1);
            if (cur > 0 && cur <= lines.size()) vcSrc.setSelectedId (cur, juce::dontSendNotification);
        }
        struct VocMeter : juce::Component, juce::Timer
        {
            explicit VocMeter (NovaProcessor& pr) : p (pr) { startTimerHz (20); }
            std::function<void()> onTick; int tick = 0;
            void timerCallback() override
            {
                const float l = p.synth.vocLevel.load(); if (std::abs (l - shown) > 0.002f) { shown = l; repaint(); }
                if (onTick && isShowing() && (tick++ % 20) == 0) onTick();   // raz za sekundu zoznam stop
            }
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour (juce::Colour (0xff2d4362)); g.fillRoundedRectangle (r, 3.0f);
                const float db = shown > 1.0e-5f ? 20.0f * std::log10 (shown) : -100.0f;
                const float f = juce::jlimit (0.0f, 1.0f, (db + 50.0f) / 50.0f);
                g.setColour (juce::Colour (0xffc9a35a)); g.fillRoundedRectangle (r.withWidth (r.getWidth() * f), 3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.85f)); g.setFont (juce::FontOptions (11.0f));
                g.drawText (f > 0.02f ? T ("Hlas prichádza") : T ("Žiadny hlas – vyber spevovú stopu v Sequenceri"), getLocalBounds().reduced (6, 0), juce::Justification::centredLeft);
            }
            NovaProcessor& p; float shown = 0;
        } vcMeter { p };
        juce::TextButton steps[16]; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stepAtt[16];
        EqGraph graph;
        std::unique_ptr<Toggle> eqOn[NEQ];
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
        bool eqShown = false; int target = 0, tab = 0;
    };

    // ===================================================================== arpeggiator + hlas
    struct ArpPanel : juce::Component
    {
        ArpPanel (NovaProcessor& pr) : on (pr.params, "arp_on", T ("ARPEGGIÁTOR"), amber.brighter (0.3f)), hold (pr.params, "arp_hold", T ("Držať"), amber.brighter (0.3f)),
            mono (pr.params, "mono", "Mono"),
            pat (pr.params, "arp_pat", juce::StringArray::fromTokens (T ("Hore|Dole|Hore/Dole|Ako hrám|Náhodne|Akord"), "|", ""), amber),
            speed (pr.params, "arp_speed", juce::StringArray::fromTokens ("1/4|1/8|1/8T|1/16|1/32", "|", ""), amber),
            oct (pr.params, "arp_oct", T ("Oktávy")), len (pr.params, "arp_len", T ("Dĺžka")), glide (pr.params, "glide"), vel (pr.params, "veltrack", T ("Citlivosť"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &hold, &mono, &pat, &speed, &oct, &len, &glide, &vel }) addAndMakeVisible (c);
            pat.setIcons ({ icUp, icDown, icUpDown, icPlayed, icRandArp, icChord });
            pat.setTips ({ T ("Arpeggio hore"), T ("Arpeggio dole"), T ("Hore a dole"), T ("V poradí, ako hráš"), T ("Náhodné poradie"), T ("Celý akord naraz v rytme") });
            oct.setArc (amber); len.setArc (amber);
            vel.s.setTooltip (T ("Ako veľmi hlasitosť závisí od sily úderu"));
            mono.setTooltip (T ("Hrá vždy len jeden tón (basy, leady) – s Glide sa tóny prelínajú"));
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            sectionLabel (g, { 12, 38, 100, 18 }, T ("VZOR"));
            sectionLabel (g, { 12, 88, 100, 18 }, T ("RÝCHLOSŤ"));
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
            setTooltip (T ("Vľavo vrstva A, vpravo vrstva B · hore jasnejší, dole tmavší · dvojklik = stred"));
        }
        juce::Rectangle<float> pad() const { const float s = juce::jmin ((float) getWidth() - 44.0f, (float) getHeight() - 64.0f); return { ((float) getWidth() - s) * 0.5f, 42.0f, s, s }; }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds(), "MORPH");
            g.setColour (dim); g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (p.synth.activeVoices()) + T (" hlasov"), getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::centredRight);
            const auto r = pad();
            for (int i = 4; i >= 1; --i) { g.setColour (steel.withAlpha (0.25f + 0.12f * (float) (4 - i))); const float k = (float) i / 4.0f; g.fillEllipse (r.withSizeKeepingCentre (r.getWidth() * k, r.getHeight() * k)); }
            g.setColour (line.brighter (0.2f)); g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY()); g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom());
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText ("A", juce::Rectangle<float> (r.getX() - 16, r.getCentreY() - 8, 14, 16), juce::Justification::centred);
            g.drawText ("B", juce::Rectangle<float> (r.getRight() + 2, r.getCentreY() - 8, 14, 16), juce::Justification::centred);
            g.drawText (T ("jasný"), juce::Rectangle<float> (r.getCentreX() - 30, r.getY() - 15, 60, 14), juce::Justification::centred);
            g.drawText (T ("tmavý"), juce::Rectangle<float> (r.getCentreX() - 30, r.getBottom() + 1, 60, 14), juce::Justification::centred);
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
            type (pr.params, "mf_type", juce::StringArray::fromTokens (T ("Dolná|Horná"), "|", ""), amber),
            cut (pr.params, "mf_cut"), reso (pr.params, "mf_reso", T ("Rezon.")), drive (pr.params, "mf_drive"), gain (pr.params, "p_gain", T ("Úroveň"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &type, &cut, &reso, &drive, &gain }) addAndMakeVisible (c);
            type.setIcons ({ icLow, icHigh });
            type.setTips ({ T ("Dolná priepusť"), T ("Horná priepusť") });
            cut.setArc (amber); reso.setArc (amber); drive.setArc (amber);
            gain.s.setTooltip (T ("Hlasitosť tohto zvuku (ukladá sa so zvukom)"));
        }
        void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds()); g.setColour (line); g.fillRect (12 + 3 * ((getWidth() - 24) / 4) - 2, 36, 1, 64); }
        void resized() override
        {
            on.setBounds (10, 6, 150, 22); type.setBounds (getWidth() - 76, 6, 64, 22);
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
        juce::StringArray cats() const;
        bool isAddChip (int i) const { return i == cats().size() - 1; }
        std::vector<juce::Rectangle<int>> layoutChips (int width) const
        {
            std::vector<juce::Rectangle<int>> r; int x = 0, y = 0;
            const juce::Font f (juce::FontOptions (13.0f));
            for (auto& c : cats())
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (f, T (c)) + 14;
                if (x + w > width && x > 0) { x = 0; y += 26; }
                r.push_back ({ x, y, w, 22 }); x += w + 5;
            }
            return r;
        }
        int heightFor (int width) const { auto r = layoutChips (width); return r.empty() ? 0 : r.back().getBottom(); }
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("preset:"); }
        void itemDragMove (const SourceDetails& d) override { const int i = chipAt (d.localPosition.toInt()); hoverDrop = isAddChip (i) ? -1 : i; repaint(); }
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
            search.setTextToShowWhenEmpty (T ("Hľadať zvuk…"), dim);
            search.addListener (this);
            search.setFont (juce::FontOptions (13.5f));
            addAndMakeVisible (search);
            importBtn.setButtonText (T ("Import zvuku…"));
            importBtn.setTooltip (T ("Pridať zvuk zo súboru .knova (napr. z iného počítača)"));
            importBtn.onClick = [this] { importDialog(); };
            folderBtn.setButtonText (T ("+ Pridať"));
            folderBtn.setTooltip (T ("Nový priečinok na zvuky, nový zvuk, zvuk z WAV, import zvukov"));
            folderBtn.onClick = [this] { addMenu(); };
            addAndMakeVisible (importBtn); addAndMakeVisible (folderBtn);
            // dve casti ako v jednoduchej verzii: Nastroje (nahrate) a Syntezator
            if (section() < 0) section() = juce::jmax (0, categorySection (p.currentCategory));
            for (int i = 0; i < 2; ++i)
            {
                secB[i].setButtonText (T (i == 0 ? "Nástroje" : "Syntezátor"));
                secB[i].setClickingTogglesState (false);
                secB[i].onClick = [this, i] { setSection (i); };
                addAndMakeVisible (secB[i]);
            }
            addAndMakeVisible (chips);
            vp.setViewedComponent (&list, false); vp.setScrollBarsShown (true, false); vp.setScrollBarThickness (8);
            addAndMakeVisible (vp);
            hint.setText (T ("Ťahaním myšou meníš poradie, pustením na priečinok ho tam presunieš. ☆ = obľúbený. Pravé tlačidlo = ďalšie možnosti."), juce::dontSendNotification);
            hint.setFont (juce::FontOptions (11.5f)); hint.setColour (juce::Label::textColourId, dim); hint.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (hint);
            refresh();
        }
        static juce::String& filterCat() { static juce::String c; return c; }
        static int& section() { static int s = -1; return s; }
        void setSection (int i)
        {
            if (i < 0 || i > 1) return;
            section() = i;
            if (filterCat().isNotEmpty() && categorySection (filterCat()) >= 0 && categorySection (filterCat()) != i) filterCat() = {};
            resized();
        }
        void textEditorTextChanged (juce::TextEditor&) override { refresh(); }
        void refresh()
        {
            p.presets.refresh();
            shown.clear();
            const auto q = search.getText().trim().toLowerCase();
            for (auto& e : p.presets.entries())
            {
                if (section() >= 0 && categorySection (e.category) >= 0 && categorySection (e.category) != section()) continue;   // druha cast
                if (filterCat() == juce::String::charToString (0x2605)) { if (! p.presets.isFav (e.key)) continue; }
                else if (filterCat().isNotEmpty() && e.category != filterCat()) continue;
                if (q.isNotEmpty() && ! e.name.toLowerCase().contains (q) && ! e.category.toLowerCase().contains (q)
                    && ! T (e.name).toLowerCase().contains (q) && ! T (e.category).toLowerCase().contains (q)) continue;
                shown.push_back (e);
            }
            list.setSize (juce::jmax (10, vp.getMaximumVisibleWidth()), (int) shown.size() * PresetList::rowH + 4);
            list.repaint(); chips.repaint();
            for (int i = 0; i < 2; ++i) secB[i].setToggleState (i == section(), juce::dontSendNotification);
        }
        void step (int d)
        {
            if (shown.empty()) return;
            int i = d > 0 ? 0 : (int) shown.size() - 1;
            for (int k = 0; k < (int) shown.size(); ++k) if (shown[(size_t) k].key == p.currentKey) i = k + d;
            i = (i + (int) shown.size()) % (int) shown.size();
            p.loadPreset (shown[(size_t) i].key); p.preview();
            list.repaint();
        }
        void importDialog()
        {
            chooser = std::make_unique<juce::FileChooser> (T ("Import zvuku"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.knova");
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
        void textDialog (const juce::String& title, const juce::String& initial, const juce::String& okText, std::function<void (const juce::String&)> done)
        {
            auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", initial, T ("Názov"));
            w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (T ("Zrušiť"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<Browser> sp (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w, done] (int r)
            {
                if (r == 1 && sp != nullptr) { const auto t = w->getTextEditorContents ("name").trim(); if (t.isNotEmpty()) done (t); }
            }), true);
        }
        void newFolderDialog()
        {
            juce::Component::SafePointer<Browser> sp (this);
            textDialog (T ("Nový priečinok na zvuky"), T ("Moje pady"), T ("Vytvoriť"), [sp] (const juce::String& n)
            {
                if (sp == nullptr) return;
                if (sp->p.presets.addCategory (n)) sp->resized();
                else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspiria", T ("Priečinok „") + n + T ("“ už existuje."));
            });
        }
        void folderMenu (const juce::String& cat)
        {
            juce::PopupMenu m;
            m.addItem (1, T ("Premenovať priečinok…"));
            m.addItem (2, T ("Zmazať priečinok (zvuky ostanú v „Moje zvuky“)"));
            juce::Component::SafePointer<Browser> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [sp, cat] (int r)
            {
                if (sp == nullptr || r == 0) return;
                if (r == 1)
                    sp->textDialog (T ("Premenovať priečinok"), cat, T ("Premenovať"), [sp, cat] (const juce::String& n)
                    {
                        if (sp == nullptr) return;
                        if (sp->p.presets.renameCategory (cat, n)) { if (filterCat() == cat) filterCat() = n; if (sp->p.currentCategory == cat) sp->p.currentCategory = n; }
                        sp->resized();
                    });
                else
                {
                    sp->p.presets.removeCategory (cat);
                    if (filterCat() == cat) filterCat() = {};
                    sp->resized();
                }
            });
        }
        void addMenu()
        {
            juce::PopupMenu m;
            m.addItem (1, T ("Nový priečinok na zvuky…"));
            m.addItem (2, T ("Nový zvuk (začať od základu)"));
            m.addItem (3, T ("Zvuk z nahrávky WAV…"));
            m.addItem (4, T ("Import zvukov (.knova)…"));
            m.addSeparator();
            m.addItem (5, T ("Otvoriť priečinok so zvukmi na disku"));
            juce::Component::SafePointer<Browser> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderBtn), [sp] (int r)
            {
                if (sp == nullptr || r == 0) return;
                auto& b = *sp;
                const auto cat = (filterCat().isNotEmpty() && filterCat() != juce::String::charToString (0x2605)) ? filterCat() : categories()[categories().size() - 1];
                if (r == 1) b.newFolderDialog();
                else if (r == 2) { b.p.newSound (cat); b.refresh(); }
                else if (r == 3) b.wavDialog (cat);
                else if (r == 4) b.importDialog();
                else if (r == 5) b.openFolder();
            });
        }
        void openFolder()
        {
            const auto dir = p.presets.folder();
            dir.createDirectory();
           #if JUCE_WINDOWS
            juce::ChildProcess cp;
            if (! cp.start ("explorer.exe \"" + dir.getFullPathName() + "\"")) dir.startAsProcess();
           #else
            dir.startAsProcess();
           #endif
        }
        void wavDialog (const juce::String& cat)
        {
            if (auto* ed = findParentComponentOfClass<NovaEditor>())
            {
                juce::Component::SafePointer<Browser> sp (this);
                ed->openWavBrowser (-1, [sp, cat] (const juce::File& f, int)
                {
                    if (sp == nullptr) return;
                    sp->p.newSound (cat);
                    juce::String err;
                    if (! sp->p.loadSampleFile (0, f, err)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspiria", err); return; }
                    sp->p.currentName = f.getFileNameWithoutExtension();
                    if (sp->p.onPatchChanged) sp->p.onPatchChanged();
                    sp->refresh();
                });
            }
        }
        void itemMenu (const PresetStore::Entry& e)
        {
            juce::PopupMenu m;
            m.addItem (1, T ("Načítať"));
            m.addItem (2, T ("Premenovať…"), ! e.factory);
            m.addItem (3, T ("Duplikovať"));
            m.addItem (6, p.presets.isFav (e.key) ? T ("Odobrať z obľúbených") : T ("Pridať medzi obľúbené"));
            m.addItem (4, T ("Exportovať…"));
            juce::PopupMenu cats;
            const auto all = p.presets.allCategories();
            for (int i = 0; i < all.size(); ++i) cats.addItem (100 + i, T (all[i]), true, e.category == all[i]);
            m.addSubMenu (T ("Presunúť do priečinka"), cats);
            m.addSeparator();
            m.addItem (5, T ("Zmazať"), ! e.factory);
            const auto key = e.key; const auto name = e.name;
            juce::Component::SafePointer<Browser> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [sp, key, name] (int r)
            {
                if (sp == nullptr || r == 0) return;
                auto& b = *sp;
                if (r == 1) b.p.loadPreset (key);
                else if (r == 2) b.renameDialog (key, T (name));
                else if (r == 3) { auto k = b.p.presets.duplicate (key); if (k.isNotEmpty()) b.p.loadPreset (k); }
                else if (r == 6) b.p.presets.setFav (key, ! b.p.presets.isFav (key));
                else if (r == 4)
                {
                    b.chooser = std::make_unique<juce::FileChooser> (T ("Exportovať zvuk"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (T (name)) + ".knova"), "*.knova");
                    b.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [sp, key] (const juce::FileChooser& fc)
                    { if (sp != nullptr && fc.getResult() != juce::File()) sp->p.presets.exportTo (key, fc.getResult().withFileExtension ("knova")); });
                }
                else if (r == 5)
                {
                    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, T ("Zmazať zvuk"), T ("Naozaj zmazať zvuk „") + T (name) + T ("“?"), T ("Zmazať"), T ("Zrušiť"), nullptr,
                        juce::ModalCallbackFunction::create ([sp, key] (int ok) { if (ok && sp != nullptr) { sp->p.presets.remove (key); sp->refresh(); } }));
                }
                else if (r >= 100) b.p.presets.setCategory (key, b.p.presets.allCategories()[r - 100]);
                b.resized();
            });
        }
        void renameDialog (const juce::String& key, const juce::String& name)
        {
            auto* w = new juce::AlertWindow (T ("Premenovať zvuk"), {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", name, T ("Nový názov"));
            w->addButton (T ("Premenovať"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (T ("Zrušiť"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
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
        void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), T ("ZVUKY")); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 0);
            r.removeFromTop (32);
            {
                auto sr = r.removeFromTop (28);
                const int w = (sr.getWidth() - 6) / 2;
                secB[0].setBounds (sr.removeFromLeft (w)); secB[1].setBounds (sr.removeFromRight (w));
                r.removeFromTop (8);
            }
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
        juce::TextEditor search; juce::TextButton importBtn, folderBtn, secB[2];
        CategoryChips chips; PresetList list; juce::Viewport vp; juce::Label hint;
        std::vector<PresetStore::Entry> shown;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    juce::StringArray CategoryChips::cats() const
    {
        juce::StringArray c; c.add (T ("Všetko")); c.add (juce::String::charToString (0x2605));
        for (auto& k : br.p.presets.allCategories())   // kategorie zvolenej casti + vlastne priecinky
            if (categorySection (k) < 0 || categorySection (k) == Browser::section()) c.add (k);
        c.add ("+");
        return c;
    }
    void CategoryChips::mouseDown (const juce::MouseEvent& e)
    {
        const int i = chipAt (e.getPosition());
        if (e.mods.isPopupMenu() && i > 1 && ! isAddChip (i) && br.p.presets.isUserCategory (cats()[i])) br.folderMenu (cats()[i]);
    }
    void CategoryChips::paint (juce::Graphics& g)
    {
        auto r = layoutChips (getWidth()); auto c = cats();
        for (int i = 0; i < (int) r.size(); ++i)
        {
            const bool sel = (i == 0 && Browser::filterCat().isEmpty()) || (i > 0 && Browser::filterCat() == c[i]);
            g.setColour (i == hoverDrop && i > 0 ? amber : (sel ? steelHi : steel));
            g.fillRoundedRectangle (r[(size_t) i].toFloat(), 3.0f);
            g.setColour (sel ? juce::Colours::white : text.withAlpha (0.85f)); g.setFont (juce::FontOptions (13.0f));
            g.drawText (T (c[i]), r[(size_t) i], juce::Justification::centred);
        }
    }
    void CategoryChips::mouseUp (const juce::MouseEvent& e)
    {
        const int i = chipAt (e.getPosition());
        if (i < 0 || e.mods.isPopupMenu()) return;
        if (isAddChip (i)) { br.newFolderDialog(); return; }
        Browser::filterCat() = i == 0 ? juce::String() : cats()[i];
        br.refresh();
    }
    void CategoryChips::itemDropped (const SourceDetails& d)
    {
        const int i = chipAt (d.localPosition.toInt()); hoverDrop = -1;
        const auto key = d.description.toString().fromFirstOccurrenceOf ("preset:", false, false);
        if (i == 1) { br.p.presets.setFav (key, true); br.refresh(); }
        else if (i > 1 && ! isAddChip (i)) { br.p.presets.setCategory (key, cats()[i]); br.refresh(); }
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
            const bool fav = br.p.presets.isFav (e.key);
            g.setColour (fav ? amber.brighter (0.4f) : (sel ? text.withAlpha (0.5f) : line.brighter (0.5f))); g.setFont (juce::FontOptions (14.0f));
            g.drawText (juce::String::charToString (fav ? 0x2605 : 0x2606), r.withWidth (22), juce::Justification::centred);
            g.setColour (sel ? juce::Colours::white : text); g.setFont (juce::FontOptions (14.0f));
            g.drawText (T (e.name), r.reduced (8, 0).withTrimmedLeft (16), juce::Justification::centredLeft);
            g.setColour (sel ? text : dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText (T (e.category), r.reduced (8, 0), juce::Justification::centredRight);
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
                g.drawText (T (en.name), img.getBounds().reduced (8, 0), juce::Justification::centredLeft);
            }
            c->startDragging ("preset:" + en.key, this, juce::ScaledImage (img), false, nullptr, &e.source);
        }
    }
    void PresetList::mouseUp (const juce::MouseEvent& e)
    {
        if (dragging || e.mods.isPopupMenu()) { dragging = false; return; }
        const int r = e.y / rowH;
        if (r == downRow && juce::isPositiveAndBelow (r, (int) br.shown.size()))
        {
            const auto key = br.shown[(size_t) r].key;
            if (e.x < 22) { br.p.presets.setFav (key, ! br.p.presets.isFav (key)); br.refresh(); return; }   // hviezdicka
            br.p.loadPreset (key); br.p.preview(); repaint();
        }
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
            // jedno tlacidlo: zapnute = rozsirena verzia, klik = spat na jednoduchu
            advanced.setButtonText (T ("Rozšírená"));
            advanced.setToggleState (true, juce::dontSendNotification);
            advanced.setTooltip (T ("Vypnúť = jednoduchá verzia so šiestimi gombíkmi"));
            advanced.onClick = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (0); }); };
            setupLearn (p, learnB);
            prev.setButtonText (juce::String::charToString (0x25C0)); next.setButtonText (juce::String::charToString (0x25B6));
            prev.onClick = [this] { br.step (-1); }; next.onClick = [this] { br.step (1); };
            save.setButtonText (T ("Uložiť…")); save.onClick = [this] { saveDialog(); };
            undoB.setButtonText (T ("Späť")); redoB.setButtonText (T ("Znova"));
            undoB.onClick = [this] { p.undo.undo(); }; redoB.onClick = [this] { p.undo.redo(); };
            setupSurprise (p, surprise, surCat, mutateB, [this] { br.list.repaint(); });
            setupFx (p, fxB, fxAtt); setupPreview (p, pvB);
            for (int s : { 75, 90, 100, 110, 125, 150 }) size.addItem (juce::String (s) + " %", s);
            size.setWantsKeyboardFocus (false);
            size.setSelectedId (juce::roundToInt (p.uiScale * 100.0f), juce::dontSendNotification);
            if (size.getSelectedId() == 0) size.setSelectedId (100, juce::dontSendNotification);
            size.onChange = [this] { const float s = (float) size.getSelectedId() / 100.0f; juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed), s] { if (sp != nullptr) sp->setScale (s); }); };
            vol.setSliderStyle (juce::Slider::LinearHorizontal); vol.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params, "volume", vol);
            vol.setDoubleClickReturnValue (true, 0.0);
            for (auto* c : std::initializer_list<juce::Component*> { &advanced, &pvB, &fxB, &prev, &next, &save, &undoB, &redoB, &surprise, &surCat, &mutateB, &learnB, &size, &vol }) addAndMakeVisible (c);
            tick();
        }
        void saveDialog()
        {
            auto* w = new juce::AlertWindow (T ("Uložiť zvuk"), T ("Zvuk sa uloží medzi vlastné zvuky."), juce::MessageBoxIconType::NoIcon);
            const bool factory = p.currentKey.startsWith ("f:");
            w->addTextEditor ("name", factory ? T (p.currentName) + T (" (môj)") : T (p.currentName), T ("Názov"));
            const auto all = p.presets.allCategories();
            juce::StringArray shown; for (auto& c : all) shown.add (T (c));
            w->addComboBox ("cat", shown, T ("Priečinok"));
            if (auto* cb = w->getComboBoxComponent ("cat"))
            {
                const int ci = all.indexOf (Browser::filterCat().isNotEmpty() && all.contains (Browser::filterCat()) && ! p.currentKey.startsWith ("u:") ? Browser::filterCat() : p.currentCategory);
                cb->setSelectedItemIndex (ci >= 0 ? ci : all.size() - 1);
            }
            w->addButton (T ("Uložiť"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (T ("Zrušiť"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<TopBar> sp (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r)
            {
                if (r != 1 || sp == nullptr) return;
                const auto name = w->getTextEditorContents ("name").trim();
                const auto all = sp->p.presets.allCategories();
                const int ci = w->getComboBoxComponent ("cat")->getSelectedItemIndex();
                const auto cat = juce::isPositiveAndBelow (ci, all.size()) ? all[ci] : all[all.size() - 1];
                if (name.isEmpty()) return;
                sp->p.savePreset (name, cat);
                sp->br.refresh();
            }), true);
        }
        void tick()
        {
            if (p.currentKey != lastKey)
            {
                lastKey = p.currentKey;
                const int sc = categorySection (p.currentCategory);
                if (sc >= 0 && sc != Browser::section()) br.setSection (sc);
            }
            {
                const bool syn = Browser::section() != 0;   // Prekvap ma, Obmena a Nauc sa zvuk patria k syntezatoru
                for (auto* c : std::initializer_list<juce::Component*> { &surprise, &surCat, &mutateB, &learnB }) if (c->isVisible() != syn) c->setVisible (syn);
            }
            undoB.setEnabled (p.undo.canUndo()); redoB.setEnabled (p.undo.canRedo());
            if (p.currentName != shownName || p.currentCategory != shownCat) { shownName = p.currentName; shownCat = p.currentCategory; repaint(); }
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (panel); g.fillRect (getLocalBounds());
            g.setColour (juce::Colour (0xff1a2029)); g.fillRoundedRectangle (nameBox.toFloat(), 3.0f);
            g.setColour (text); g.setFont (juce::FontOptions (16.0f));
            g.drawText (T (shownName), nameBox.reduced (12, 0), juce::Justification::centredLeft);
            g.setColour (dim); g.setFont (juce::FontOptions (12.5f));
            g.drawText (T (shownCat), nameBox.reduced (12, 0), juce::Justification::centredRight);
            g.drawText (T ("Veľkosť"), size.getBounds().translated (-62, 0).withWidth (56), juce::Justification::centredRight);
            g.drawText (T ("Hlasitosť"), vol.getBounds().translated (-72, 0).withWidth (66), juce::Justification::centredRight);
           #ifndef INSPIRE_BUILD
            #define INSPIRE_BUILD 0
           #endif
            g.setFont (juce::FontOptions (10.5f));
            g.drawText ("v1." + juce::String (INSPIRE_BUILD), juce::Rectangle<int> (vol.getX(), 40, vol.getWidth(), 12), juce::Justification::centredRight);
        }
        void resized() override
        {
            const int y = 12, h = 28;
            advanced.setBounds (12, y, 96, h);
            fxB.setBounds (176, y, 40, h); pvB.setBounds (146, y, 28, h); kbB.setBounds (116, y, 28, h);
            prev.setBounds (220, y, 30, h);
            nameBox = { 254, y, 220, h };
            next.setBounds (478, y, 30, h);
            save.setBounds (516, y, 76, h); undoB.setBounds (598, y, 58, h); redoB.setBounds (660, y, 58, h);
            surprise.setBounds (728, y, 96, h); surCat.setBounds (826, y, 20, h); mutateB.setBounds (852, y, 74, h);
            learnB.setBounds (932, y, 116, h);
            vol.setBounds (getWidth() - 12 - 96, y, 96, h);
            size.setBounds (vol.getX() - 76 - 78, y + 2, 78, h - 4);
        }
        NovaEditor& ed; NovaProcessor& p; Browser& br;
        juce::TextButton advanced, prev, next, save, undoB, redoB, surprise, surCat, mutateB, learnB, fxB, pvB;
        KeysButton kbB { p };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fxAtt;
        juce::ComboBox size; juce::Slider vol;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAtt;
        juce::Rectangle<int> nameBox; juce::String shownName, shownCat, lastKey;
    };

    // ===================================================================== rozsirena verzia
    struct ExtendedView : juce::Component, Tickable
    {
        ExtendedView (NovaEditor& e) : ed (e), p (e.proc), browser (p), insp (p), top (e, browser), la (p, 0), lb (p, 1), tube (p), fx (p), arp (p), morph (p), master (p),
            keys (p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &top, &browser, &insp, &la, &lb, &tube, &fx, &arp, &morph, &master, &keys }) addAndMakeVisible (c);
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
            top.tick(); la.update(); lb.update(); tube.repaint(); morph.repaint(); fx.tick();
            if (p.currentKey != lastKey) { lastKey = p.currentKey; browser.list.repaint(); }
        }
        int layerAt (juce::Point<int> pt) const { return lb.getBounds().contains (pt) ? 1 : 0; }
        void paint (juce::Graphics& g) override { g.fillAll (bg); }
        void resized() override
        {
            top.setBounds (0, 0, getWidth(), 52);
            const int x0 = 12, y0 = 52 + 10;
            browser.setBounds (x0, y0, 240, getHeight() - y0 - 12 - 48);
            insp.setBounds (x0, getHeight() - 12 - 40, 240, 40);
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
        Browser browser; InspireButton insp; TopBar top; LayerPanel la, lb; TubePanel tube; FxPanel fx; ArpPanel arp; MorphPad morph; MasterPanel master;
        juce::MidiKeyboardComponent keys;
        juce::String lastKey;
    };
}

// ===================================================================== editor
NovaEditor::NovaEditor (NovaProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);   // klik kdekolvek vrati klavesnicu editoru (sipky prepinaju zvuky)
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
    overlay = nullptr;
    proc.onPatchChanged = nullptr;
    view = nullptr;
    setLookAndFeel (nullptr);
}

void NovaEditor::openWavBrowser (int layer, std::function<void (const juce::File&, int)> onLoad)
{
    if (view == nullptr) return;
    juce::Component::SafePointer<NovaEditor> sp (this);
    auto close = [sp] { juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->overlay = nullptr; }); };
    auto* wb = new WavBrowser (proc, layer, [onLoad, close] (const juce::File& f, int l) { if (onLoad) onLoad (f, l); close(); }, close);
    overlay.reset (wb);
    view->addAndMakeVisible (*wb);
    wb->setBounds (view->getLocalBounds());
    wb->list.grabKeyboardFocus();
}

void NovaEditor::knobMenu (juce::Component& knob, const juce::String& id)
{
    juce::PopupMenu m;
    const int cc = proc.ccFor (id);
    m.addSectionHeader (T ("MIDI ovládač"));
    m.addItem (1, T ("MIDI learn – potom pohni gombíkom na klaviatúre"));
    m.addItem (2, cc >= 0 ? T ("Zrušiť priradenie") + " (CC " + juce::String (cc) + ")" : T ("Zrušiť priradenie"), cc >= 0 || proc.isLearning (id));
    if (const auto* sp = findSpec (id)) { m.addSeparator(); m.addItem (3, T ("Pôvodná hodnota")); (void) sp; }
    juce::Component::SafePointer<NovaEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&knob), [safe, id] (int r)
    {
        if (safe == nullptr) return;
        auto& p = safe->proc;
        if (r == 1) p.midiLearn (id);
        else if (r == 2) p.midiForget (id);
        else if (r == 3) if (auto* prm = p.params.getParameter (id)) if (const auto* sp = findSpec (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (sp->def));
        if (safe->view != nullptr) safe->view->repaint();
    });
}

void NovaEditor::setMode (int mode)
{
    overlay = nullptr;
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
    const float s = proc.uiScale;   // velkost okna plati pre jednoduchu aj rozsirenu verziu
    view->setTopLeftPosition (0, 0);
    content.setTransform ({});
    content.setBounds (0, 0, view->getWidth(), view->getHeight());
    content.setTransform (juce::AffineTransform::scale (s));
    setSize (juce::roundToInt ((float) view->getWidth() * s), juce::roundToInt ((float) view->getHeight() * s));
}

void NovaEditor::paint (juce::Graphics& g) { g.fillAll (bg); }

void NovaEditor::mouseDown (const juce::MouseEvent& e)
{
    if (dynamic_cast<juce::TextEditor*> (e.originalComponent) == nullptr) grabKeyboardFocus();
}

void NovaEditor::parentHierarchyChanged()
{
    juce::Component::SafePointer<NovaEditor> sp (this);
    juce::MessageManager::callAsync ([sp] { if (sp != nullptr && sp->isShowing()) sp->grabKeyboardFocus(); });
}

// sipky: predchadzajuci / dalsi zvuk (v rozsirenej podla zoznamu, v jednoduchej tovarenske)
bool NovaEditor::keyPressed (const juce::KeyPress& k)
{
    const int kc = k.getKeyCode();
    const int d = (kc == juce::KeyPress::rightKey || kc == juce::KeyPress::downKey) ? 1 : ((kc == juce::KeyPress::leftKey || kc == juce::KeyPress::upKey) ? -1 : 0);
    if (d == 0 || k.getModifiers().isAnyModifierKeyDown()) return false;
    if (auto* ev = dynamic_cast<ExtendedView*> (view.get())) { ev->browser.step (d); ev->tick(); return true; }
    const auto& f = factoryPatches();
    int i = -1;
    for (int j = 0; j < (int) f.size(); ++j) if (proc.currentKey == "f:" + f[(size_t) j].name) i = j;
    i = i < 0 ? (d > 0 ? 0 : (int) f.size() - 1) : (i + d + (int) f.size()) % (int) f.size();
    proc.loadPreset ("f:" + f[(size_t) i].name); proc.preview();
    timerCallback();
    return true;
}
void NovaEditor::resized() {}

void NovaEditor::timerCallback()
{
    if (auto* wb = dynamic_cast<WavBrowser*> (overlay.get())) wb->tickPlay();
    if (proc.ccVersion.load() != lastCcVersion) { lastCcVersion = proc.ccVersion.load(); if (view != nullptr) view->repaint(); }
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
        if (! proc.loadSampleFile (layer, f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspiria", err);
        break;
    }
}
