#include "PluginEditor.h"
#include <complex>
#include <juce_dsp/juce_dsp.h>

using namespace nova;
using namespace nova::ui;

namespace
{
    constexpr int SW = 780, SH = 372;       // jednoducha verzia
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

    // vyber jazyka SK / EN; po zmene sa rozhranie postavi znova
    void setupLangBox (juce::ComboBox& b, NovaEditor& ed)
    {
        b.setWantsKeyboardFocus (false);
        b.addItem ("SK", 1); b.addItem ("EN", 2);
        b.setSelectedId (lang() + 1, juce::dontSendNotification);
        b.setTooltip ("Jazyk / Language");
        b.onChange = [&b, &ed]
        {
            setLang (b.getSelectedId() - 1);
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (sp->proc.uiMode); });
        };
    }


    // ===================================================================== jednoducha verzia
    struct SimpleView : juce::Component, Tickable
    {
        struct PresetButton : juce::Component
        {
            juce::String name, key, cat; bool selected = false, hover = false, small = false; std::function<void()> onClick;
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (selected ? (small ? amber : steelHi) : (hover ? steel.brighter (0.12f) : (small ? panel : steel)));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (selected ? juce::Colours::white : text.withAlpha (0.9f));
                g.setFont (juce::FontOptions (small ? 13.0f : 14.0f, selected ? juce::Font::bold : juce::Font::plain));
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
                b->name = T (f.name); b->key = "f:" + f.name; b->cat = f.category;
                auto key = b->key;
                b->onClick = [this, key] { p.loadPreset (key); tick(); };
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
            {
                const auto* e = p.presets.find (p.currentKey);
                const auto saved = p.presets.setting ("simpleCat");
                setCat (e != nullptr && e->factory ? factoryPatches()[(size_t) e->factoryIndex].category : (saved.isNotEmpty() ? saved : categories()[0]));
            }
            for (auto id : { "bright", "attack", "release", "space", "width", "volume" })
            {
                auto k = std::make_unique<Knob> (p.params, id, juce::String (id) == "release" ? T ("Dozvuk", "Release") : juce::String());
                k->label.setFont (juce::FontOptions (14.0f)); k->label.setColour (juce::Label::textColourId, text);
                addAndMakeVisible (*k);
                knobs.push_back (std::move (k));
            }
            ext.setButtonText (T ("Rozšírená ▸"));
            ext.setTooltip (T ("Úplný editor zvuku: vrstvy, filtre, efekty, arpeggiátor, vlastné zvuky"));
            ext.onClick = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (1); }); };
            addAndMakeVisible (ext);
            setupSurprise (p, surprise, surCat, mutateB, [this] { tick(); });
            addAndMakeVisible (surprise); addAndMakeVisible (surCat); addAndMakeVisible (mutateB);
            setupLangBox (langBox, ed); addAndMakeVisible (langBox);
            setSize (SW, SH);
            tick();
        }
        void setCat (const juce::String& c)
        {
            curCat = c; p.presets.setSetting ("simpleCat", c);
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
                    setCat (factoryPatches()[(size_t) e->factoryIndex].category);
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
            g.drawText ("KORGUS INSPIRE", head.reduced (16, 0), juce::Justification::centredLeft);
            g.setColour (dim); g.setFont (juce::FontOptions (13.0f));
            g.drawText (status, head.reduced (16, 0).withTrimmedRight (420).withTrimmedLeft (170), juce::Justification::centredRight);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (T ("Klavír: Salamander Grand Piano V3 – Alexander Holm, licencia CC-BY 3.0"), getLocalBounds().removeFromBottom (22).reduced (16, 0), juce::Justification::centredLeft);
        }
        void resized() override
        {
            ext.setBounds (getWidth() - 16 - 112, 9, 112, 26);
            mutateB.setBounds (ext.getX() - 8 - 84, 9, 84, 26);
            surCat.setBounds (mutateB.getX() - 6 - 22, 9, 22, 26);
            surprise.setBounds (surCat.getX() - 2 - 104, 9, 104, 26);
            langBox.setBounds (surprise.getX() - 8 - 58, 11, 58, 22);
            auto r = getLocalBounds().reduced (16, 0);
            r.removeFromTop (44 + 10);
            {
                auto cr = r.removeFromTop (26);
                const int n = (int) catButtons.size(), g2 = 4, cw = (cr.getWidth() - g2 * (n - 1)) / juce::jmax (1, n);
                for (int i = 0; i < n; ++i) catButtons[(size_t) i]->setBounds (cr.getX() + i * (cw + g2), cr.getY(), cw, 26);
            }
            r.removeFromTop (8);
            const int cols = 5, gap = 6, bh = 42;
            const int bw = (r.getWidth() - gap * (cols - 1)) / cols;
            int k = 0;
            for (auto& b : buttons)
                if (b->isVisible()) { b->setBounds (r.getX() + (k % cols) * (bw + gap), r.getY() + (k / cols) * (bh + gap), bw, bh); ++k; }
            r.removeFromTop (2 * (bh + gap) + 6);
            auto kr = r.removeFromTop (140);
            const int kw = kr.getWidth() / 6;
            for (auto& k : knobs) k->setBounds (kr.removeFromLeft (kw).reduced (12, 0));
        }
        NovaEditor& ed; NovaProcessor& p;
        std::vector<std::unique_ptr<PresetButton>> buttons, catButtons;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::String curCat, lastKey;
        juce::TextButton ext, surprise, surCat, mutateB;
        juce::ComboBox langBox;
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

    // ===================================================================== vrstva A / B
    struct LayerPanel : juce::Component
    {
        LayerPanel (NovaProcessor& pr, int layer) : p (pr), li (layer), pre (layer == 0 ? "a_" : "b_"),
            on (pr.params, pre + "on", layer == 0 ? T ("VRSTVA A") : T ("VRSTVA B"), layer == 0 ? arc : arcB),
            wave (pr.params, pre + "wave", juce::StringArray::fromTokens (T ("Sín|Troj|Píla|Obdĺž|Šum|Pluck|FM|Sampel"), "|", ""), layer == 0 ? steelHi : accentB),
            ftype (pr.params, pre + "ftype"),
            f24 (pr.params, pre + "f24", "24 dB", layer == 0 ? arc : arcB),
            lshape (pr.params, pre + "lshape"), ltgt (pr.params, pre + "ltgt"), ratio (pr.params, pre + "ratio"), src (pr.params, pre + "src"),
            loop (pr.params, pre + "loop", T ("Opakovať"), layer == 0 ? arc : arcB)
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
                row1.push_back (mk (r1[i], T (t1[i])));
                row2.push_back (mk (r2[i], T (t2[i])));
                row3.push_back (mk (r3[i], T (t3[i])));
            }
            color = mk ("color", {});
            root = mk ("root", T ("Zákl. tón"));
            load.setButtonText (T ("Načítať WAV…"));
            load.setTooltip (T ("Vlastný zvuk do vrstvy (alebo pretiahni WAV súbor myšou na vrstvu)"));
            load.onClick = [this] { chooseFile(); };
            addAndMakeVisible (load);
            smpName.setFont (juce::FontOptions (12.5f)); smpName.setColour (juce::Label::textColourId, dim.brighter (0.3f));
            addAndMakeVisible (smpName);
            ratioLabel.setText (T ("FM pomer"), juce::dontSendNotification); ratioLabel.setFont (juce::FontOptions (12.5f));
            ratioLabel.setColour (juce::Label::textColourId, dim.brighter (0.2f)); ratioLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (ratioLabel);
            src.setTooltip (T ("Klavír = vstavané nahrávky klavíra, Vlastný WAV = tvoj zvuk"));
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
            if (auto* ed = findParentComponentOfClass<NovaEditor>())
            {
                juce::Component::SafePointer<LayerPanel> sp (this);
                ed->openWavBrowser (li, [sp] (const juce::File& f, int layer)
                {
                    if (sp == nullptr) return;
                    juce::String err;
                    if (! sp->p.loadSampleFile (layer, f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspire", err);
                    sp->update();
                });
            }
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
                color->setTitle (T (ct));
                color->setVisible (w >= 3 && ! isUser);
                ratio.setVisible (w == 6); ratioLabel.setVisible (w == 6);
                src.setVisible (isSample); load.setVisible (isSample); root->setVisible (isUser); loop.setVisible (isUser); smpName.setVisible (isUser);
                row1[3]->setEnabled (w <= 3); row1[4]->setEnabled (w <= 3);
                resized();
            }
            const auto nm = p.sampleName (li);
            smpName.setText (nm.isNotEmpty() ? nm : T ("(žiadny zvuk)"), juce::dontSendNotification);
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            sectionLabel (g, { 12, 118, 50, 20 }, "FILTER");
            sectionLabel (g, { 12 + 3 * 60, 118, 130, 20 }, T ("OBÁLKA FILTRA"));
            sectionLabel (g, { 12, 222, 160, 20 }, T ("OBÁLKA HLASITOSTI"));
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
        TubePanel (NovaProcessor& pr) : p (pr), analog (pr.params, "tube", T ("Analóg")), comp (pr.params, "comp", T ("Kompresia")), compOn (pr.params, "comp_on", T ("Tube komp"), amber.brighter (0.3f))
        {
            addAndMakeVisible (analog); addAndMakeVisible (comp); addAndMakeVisible (compOn);
            analog.setArc (amber); comp.setArc (amber);
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds(), "TUBE", amber.brighter (0.3f));
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText (T ("Teplo a jemné\n„plávanie“ ladenia"), juce::Rectangle<int> (10, 140, getWidth() - 20, 32), juce::Justification::centred, 2);
            g.drawFittedText (T ("Vyrovná hlasité tóny"), juce::Rectangle<int> (10, 326, getWidth() - 20, 18), juce::Justification::centred, 1);
            auto bar = juce::Rectangle<float> (14.0f, 360.0f, (float) getWidth() - 28.0f, 8.0f);
            g.setColour (steel); g.fillRoundedRectangle (bar, 3.0f);
            const float gr = juce::jlimit (0.0f, 20.0f, p.synth.gainReduction());
            g.setColour (amber); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * gr / 20.0f), 3.0f);
            g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
            g.drawText (T ("Stíšenie ") + juce::String (gr, 1) + " dB", juce::Rectangle<int> (10, 372, getWidth() - 20, 18), juce::Justification::centred);
        }
        void resized() override
        {
            analog.setBounds (20, 36, getWidth() - 40, 100);
            compOn.setBounds (14, 190, getWidth() - 20, 24);
            comp.setBounds (25, 220, getWidth() - 50, 100);
        }
        NovaProcessor& p; Knob analog, comp; Toggle compOn;
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
        void setTarget (const juce::String& prefix, juce::Colour c, int tap)
        {
            col = c; tapIdx = tap; lastW = 0;
            std::fill (spec.begin(), spec.end(), -120.0f);
            updateTap();
            const bool snd = prefix == "eq_";
            ids = snd ? juce::StringArray { "eq_low", "eq_mid", "eq_midf", "eq_high" }
                      : juce::StringArray { prefix + "eql", prefix + "eqm", prefix + "eqmf", prefix + "eqh" };
            atts.clear();
            for (int i = 0; i < 4; ++i)
            {
                atts.push_back (std::make_unique<juce::ParameterAttachment> (*p.params.getParameter (ids[i]), [this, i] (float v) { val[i] = v; repaint(); }, &p.undo));
                atts.back()->sendInitialUpdate();
            }
        }
        static constexpr float fLo = 20.0f, fHi = 20000.0f, dbR = 18.0f;
        float xOf (float hz) const { return (float) getWidth() * std::log (hz / fLo) / std::log (fHi / fLo); }
        float hzOf (float x) const { return fLo * std::pow (fHi / fLo, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
        float yOf (float db) const { return graphH() * 0.5f - db / dbR * (graphH() * 0.5f - 6.0f); }
        float dbOf (float y) const { return -(y - graphH() * 0.5f) / (graphH() * 0.5f - 6.0f) * dbR; }
        float graphH() const { return (float) getHeight() - 18.0f; }
        juce::Point<float> pt (int k) const
        {
            if (k == 0) return { xOf (150.0f), yOf (val[0]) };
            if (k == 1) return { xOf (val[2]), yOf (val[1]) };
            return { xOf (2500.0f), yOf (val[3]) };
        }
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
            // krivka
            Biquad b[3];
            b[0].set (0, 150, 0.7f, val[0], 48000); b[1].set (1, val[2], 0.8f, val[1], 48000); b[2].set (2, 2500, 0.7f, val[3], 48000);
            juce::Path path;
            for (int x = 0; x <= getWidth(); x += 2)
            {
                const double w = juce::MathConstants<double>::twoPi * hzOf ((float) x) / 48000.0;
                const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
                double mag = 1;
                for (auto& q : b) mag *= std::abs (((double) q.b0 + (double) q.b1 * z1 + (double) q.b2 * z2) / (1.0 + (double) q.a1 * z1 + (double) q.a2 * z2));
                const float y = juce::jlimit (0.0f, graphH(), yOf ((float) (20.0 * std::log10 (mag))));
                if (x == 0) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
            }
            g.setColour (enabled ? col : col.withAlpha (0.35f)); g.strokePath (path, juce::PathStrokeType (2.5f));
            for (int k = 0; k < 3; ++k)
            {
                const auto c = pt (k);
                g.setColour (k == drag ? text : col); g.fillEllipse (c.x - 8, c.y - 8, 16, 16);
            }
        }
        int hit (juce::Point<float> m) const
        {
            int best = -1; float bd = 18.0f;
            for (int k = 0; k < 3; ++k) { const float d = pt (k).getDistanceFrom (m); if (d < bd) { bd = d; best = k; } }
            return best;
        }
        int dbParam (int k) const { return k == 0 ? 0 : (k == 1 ? 1 : 3); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            drag = hit (e.position);
            if (drag < 0) return;
            atts[(size_t) dbParam (drag)]->beginGesture();
            if (drag == 1) atts[2]->beginGesture();
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (drag < 0) return;
            atts[(size_t) dbParam (drag)]->setValueAsPartOfGesture (juce::jlimit (-15.0f, 15.0f, dbOf (e.position.y)));
            if (drag == 1) atts[2]->setValueAsPartOfGesture (juce::jlimit (200.0f, 8000.0f, hzOf (e.position.x)));
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (drag < 0) return;
            atts[(size_t) dbParam (drag)]->endGesture();
            if (drag == 1) atts[2]->endGesture();
            drag = -1; repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int k = hit (e.position);
            if (k < 0) return;
            atts[(size_t) dbParam (k)]->setValueAsCompleteGesture (0);
            if (k == 1) atts[2]->setValueAsCompleteGesture (1000);
        }
        void flat()
        {
            p.undo.beginNewTransaction (T ("Plochý EQ"));
            for (int i = 0; i < 4; ++i) atts[(size_t) i]->setValueAsCompleteGesture (i == 2 ? 1000.0f : 0.0f);
        }
        NovaProcessor& p; juce::StringArray ids; juce::Colour col = arc;
        std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
        float val[4] { 0, 0, 1000, 0 }; int drag = -1;
        bool enabled = true; int tapIdx = 0; unsigned lastW = 0; float sr = 48000;
        juce::dsp::FFT fft; std::vector<float> spec;
    };

    // ===================================================================== efekty + EQ
    // ciele EQ: 0 vrstva A, 1 vrstva B, 2 cely zvuk, 3 delay, 4 dozvuk
    constexpr int NEQ = 5;
    const char* eqPrefix (int t) { static const char* p[NEQ] = { "a_", "b_", "eq_", "dl_", "rv_" }; return p[t]; }
    int eqTap (int t) { static const int tp[NEQ] = { 0, 1, 2, 4, 3 }; return tp[t]; }
    juce::Colour eqColour (int t) { return t == 0 ? arc : (t == 1 ? arcB : (t == 2 ? text.darker (0.15f) : (t == 3 ? juce::Colour (0xff7fa89a) : amber.brighter (0.25f)))); }

    struct FxPanel : juce::Component
    {
        FxPanel (NovaProcessor& pr) : p (pr),
            drive (pr.params, "drive_on"), chorus (pr.params, "ch_on"), delay (pr.params, "dl_on"), reverb (pr.params, "rv_on"), pp (pr.params, "dl_pp"),
            sync (pr.params, "dl_sync"), chType (pr.params, "ch_type"), graph (pr)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &drive, &chorus, &delay, &reverb, &pp, &sync, &chType }) addAndMakeVisible (c);
            chType.setTooltip (T ("Chorus = širší zvuk, Ensemble = sláčikový ensemble, Flanger a Phaser = „lietadlo“, Tremolo = kolísanie hlasitosti"));
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
            const char* tn[] = { "Vrstva A", "Vrstva B", "Zvuk", "Delay", "Dozvuk" };
            const char* tt[] = { "EQ len pre vrstvu A", "EQ len pre vrstvu B", "EQ celého zvuku", "EQ len pre opakovania delayu (napr. ubrať basy a výšky, aby delay nezahlcoval)", "EQ len pre dozvuk (napr. ubrať basy, aby dozvuk nehučal)" };
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
            flatBtn.setButtonText (T ("Plochý")); flatBtn.setTooltip (T ("Vynulovať tento EQ"));
            flatBtn.onClick = [this] { graph.flat(); };
            addAndMakeVisible (flatBtn); addAndMakeVisible (graph);
            help.setText (T ("Ťahaj body v grafe:\nhore / dole = hlasnejšie alebo tichšie,\nstredný aj vľavo / vpravo.\nDvojklik na bod ho vynuluje."), juce::dontSendNotification);
            help.setFont (juce::FontOptions (12.0f)); help.setColour (juce::Label::textColourId, dim); help.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (help);
            for (auto* b : { &tabFx, &tabEq }) addAndMakeVisible (b);
            tabFx.setButtonText (T ("EFEKTY")); tabEq.setButtonText ("EQ");
            tabFx.onClick = [this] { showEq (false); }; tabEq.onClick = [this] { showEq (true); };
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
            showEq (false);
        }
        void setTarget (int t)
        {
            target = t;
            for (int i = 0; i < NEQ; ++i) targets[i].setToggleState (i == t, juce::dontSendNotification);
            graph.setTarget (eqPrefix (t), eqColour (t), eqTap (t));
            showEq (eqShown);
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
        void showEq (bool e)
        {
            eqShown = e;
            tabFx.setToggleState (! e, juce::dontSendNotification); tabEq.setToggleState (e, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &drive, &chorus, &delay, &reverb, &pp, &sync, &chType }) c->setVisible (! e);
            for (auto& k : fx) k->setVisible (! e);
            for (int i = 0; i < (int) eq.size(); ++i) eq[(size_t) i]->setVisible (e && i / 4 == target);
            for (auto& b : targets) b.setVisible (e);
            flatBtn.setVisible (e); graph.setVisible (e); help.setVisible (e);
            for (int t = 0; t < NEQ; ++t) eqOn[t]->setVisible (e && t == target);
            syncEnabled();
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            if (! eqShown) { g.setColour (line); g.fillRect (276, 44, 1, 196); }
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
            const int x2 = 286;
            chorus.setBounds (x2, 40, 110, 22); chType.setBounds (x2 + 114, 40, 116, 22);
            const int chk[4] = { 1, 2, 11, 3 }, rvk[4] = { 8, 12, 9, 10 };
            for (int i = 0; i < 4; ++i) fx[(size_t) chk[i]]->setBounds (x2 + i * 58, 64, 56, 70);
            reverb.setBounds (x2, 142, 100, 22);
            for (int i = 0; i < 4; ++i) fx[(size_t) rvk[i]]->setBounds (x2 + i * 58, 166, 56, 72);
            // EQ
            for (int t = 0; t < NEQ; ++t) targets[t].setBounds (12 + t * 66, 38, 63, 24);
            flatBtn.setBounds (getWidth() - 12 - 70, 38, 70, 24);
            for (auto& tg : eqOn) tg->setBounds (flatBtn.getX() - 6 - 96, 39, 96, 22);
            for (int i = 0; i < (int) eq.size(); ++i) eq[(size_t) i]->setBounds (12 + (i % 4) * 62, 66, 58, 70);
            help.setBounds (270, 70, getWidth() - 282, 64);
            graph.setBounds (12, 140, getWidth() - 24, getHeight() - 150);
        }
        NovaProcessor& p;
        Toggle drive, chorus, delay, reverb, pp; Combo sync, chType;
        std::vector<std::unique_ptr<Knob>> fx, eq;
        juce::TextButton tabFx, tabEq, targets[NEQ], flatBtn; juce::Slider width, punch; juce::Label widthL, punchL, help;
        EqGraph graph;
        std::unique_ptr<Toggle> eqOn[NEQ];
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
        bool eqShown = false; int target = 0;
    };

    // ===================================================================== arpeggiator + hlas
    struct ArpPanel : juce::Component
    {
        ArpPanel (NovaProcessor& pr) : on (pr.params, "arp_on", T ("ARPEGGIÁTOR"), amber.brighter (0.3f)), hold (pr.params, "arp_hold", T ("Držať"), amber.brighter (0.3f)),
            mono (pr.params, "mono", "Mono"),
            pat (pr.params, "arp_pat", juce::StringArray::fromTokens (T ("Hore|Dole|H/D|Hrám|Náhod.|Akord"), "|", ""), amber),
            speed (pr.params, "arp_speed", juce::StringArray::fromTokens ("1/4|1/8|1/8T|1/16|1/32", "|", ""), amber),
            oct (pr.params, "arp_oct", T ("Oktávy")), len (pr.params, "arp_len", T ("Dĺžka")), glide (pr.params, "glide"), vel (pr.params, "veltrack", T ("Citlivosť"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &hold, &mono, &pat, &speed, &oct, &len, &glide, &vel }) addAndMakeVisible (c);
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
            cut.setArc (amber); reso.setArc (amber); drive.setArc (amber);
            gain.s.setTooltip (T ("Hlasitosť tohto zvuku (ukladá sa so zvukom)"));
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
            addAndMakeVisible (chips);
            vp.setViewedComponent (&list, false); vp.setScrollBarsShown (true, false); vp.setScrollBarThickness (8);
            addAndMakeVisible (vp);
            hint.setText (T ("Ťahaním myšou meníš poradie, pustením na priečinok ho tam presunieš. ☆ = obľúbený. Pravé tlačidlo = ďalšie možnosti."), juce::dontSendNotification);
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
                if (filterCat() == juce::String::charToString (0x2605)) { if (! p.presets.isFav (e.key)) continue; }
                else if (filterCat().isNotEmpty() && e.category != filterCat()) continue;
                if (q.isNotEmpty() && ! e.name.toLowerCase().contains (q) && ! e.category.toLowerCase().contains (q)
                    && ! T (e.name).toLowerCase().contains (q) && ! T (e.category).toLowerCase().contains (q)) continue;
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
                else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspire", T ("Priečinok „") + n + T ("“ už existuje."));
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
                    if (! sp->p.loadSampleFile (0, f, err)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspire", err); return; }
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

    juce::StringArray CategoryChips::cats() const
    {
        juce::StringArray c; c.add (T ("Všetko")); c.add (juce::String::charToString (0x2605)); c.addArray (br.p.presets.allCategories()); c.add ("+");
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
            br.p.loadPreset (key); repaint();
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
            simple.setButtonText (T ("Jednoduchá")); advanced.setButtonText (T ("Rozšírená"));
            advanced.setToggleState (true, juce::dontSendNotification);
            simple.onClick = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed)] { if (sp != nullptr) sp->setMode (0); }); };
            prev.setButtonText (juce::String::charToString (0x25C0)); next.setButtonText (juce::String::charToString (0x25B6));
            prev.onClick = [this] { br.step (-1); }; next.onClick = [this] { br.step (1); };
            save.setButtonText (T ("Uložiť…")); save.onClick = [this] { saveDialog(); };
            undoB.setButtonText (T ("Späť")); redoB.setButtonText (T ("Znova"));
            undoB.onClick = [this] { p.undo.undo(); }; redoB.onClick = [this] { p.undo.redo(); };
            setupSurprise (p, surprise, surCat, mutateB, [this] { br.list.repaint(); });
            for (int s : { 75, 90, 100, 110, 125, 150 }) size.addItem (juce::String (s) + " %", s);
            size.setWantsKeyboardFocus (false);
            size.setSelectedId (juce::roundToInt (p.uiScale * 100.0f), juce::dontSendNotification);
            if (size.getSelectedId() == 0) size.setSelectedId (100, juce::dontSendNotification);
            size.onChange = [this] { const float s = (float) size.getSelectedId() / 100.0f; juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<NovaEditor> (&ed), s] { if (sp != nullptr) sp->setScale (s); }); };
            vol.setSliderStyle (juce::Slider::LinearHorizontal); vol.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params, "volume", vol);
            vol.setDoubleClickReturnValue (true, 0.0);
            for (auto* c : std::initializer_list<juce::Component*> { &simple, &advanced, &prev, &next, &save, &undoB, &redoB, &surprise, &surCat, &mutateB, &langBox, &size, &vol }) addAndMakeVisible (c);
            setupLangBox (langBox, ed);
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
        }
        void resized() override
        {
            const int y = 12, h = 28;
            simple.setBounds (12, y, 96, h); advanced.setBounds (110, y, 96, h);
            prev.setBounds (220, y, 30, h);
            nameBox = { 254, y, 220, h };
            next.setBounds (478, y, 30, h);
            save.setBounds (516, y, 76, h); undoB.setBounds (598, y, 58, h); redoB.setBounds (660, y, 58, h);
            surprise.setBounds (728, y, 96, h); surCat.setBounds (826, y, 20, h); mutateB.setBounds (852, y, 74, h);
            langBox.setBounds (932, y + 2, 58, h - 4);
            vol.setBounds (getWidth() - 12 - 96, y, 96, h);
            size.setBounds (vol.getX() - 76 - 78, y + 2, 78, h - 4);
        }
        NovaEditor& ed; NovaProcessor& p; Browser& br;
        juce::TextButton simple, advanced, prev, next, save, undoB, redoB, surprise, surCat, mutateB;
        juce::ComboBox size, langBox; juce::Slider vol;
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
            top.tick(); la.update(); lb.update(); tube.repaint(); morph.repaint(); fx.tick();
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
    const float s = proc.uiMode == 1 ? proc.uiScale : 1.0f;
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
    proc.loadPreset ("f:" + f[(size_t) i].name);
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
        if (! proc.loadSampleFile (layer, f, err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Korgus Inspire", err);
        break;
    }
}
