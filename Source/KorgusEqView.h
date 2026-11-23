#pragma once
// Spolocny graficky ekvalizer pre Korgus Sequencer (Korgus EQ) aj Korgus Inspiria.
// Vyzera aj ovlada sa vsade rovnako: body pasiem sa tahaju mysou, vybrane pasmo ma uchytky sirky.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <atomic>
#include <algorithm>
#include <vector>

namespace korgus
{
// referencna pesnicka pre asistenta EQ (tretinooktavove spektrum v dB, 31 pasiem od 20 Hz); nastavuje Sequencer
struct EqReference { float db[31] {}; std::atomic<bool> valid { false }; };
inline EqReference& eqReference() { static EqReference r; return r; }

struct EqBand
{
    int type = 1;                 // 0 basy (shelf), 1 spicka, 2 vysky (shelf), 3 orezanie basov, 4 orezanie vysok
    float f = 1000, g = 0, q = 1;
    bool hasF = true, hasG = true, hasQ = true;
    float fMin = 20, fMax = 20000, fDef = 1000, qMin = 0.3f, qMax = 8, qDef = 1;
    bool active = true;
};

struct EqModel
{
    virtual ~EqModel() = default;
    virtual int numBands() const = 0;
    virtual EqBand band (int i) const = 0;
    virtual void setF (int i, float v) = 0;
    virtual void setG (int i, float v) = 0;
    virtual void setQ (int i, float v) = 0;
    virtual void begin (int i) = 0;
    virtual void end (int i) = 0;
    virtual double magnitude (float hz) const = 0;            // celkova odozva (nasobok)
    virtual double bandMagnitude (int i, float hz) const = 0;  // odozva jedneho pasma
    virtual double bandMagnitudeAt (int i, float hz, float gainDb) const { juce::ignoreUnused (gainDb); return bandMagnitude (i, hz); }
    virtual float spectrumDb (float hz) const { juce::ignoreUnused (hz); return -200.0f; }   // -200 = bez spektra
    virtual bool enabled() const { return true; }
    virtual float spectrumDbRaw (float hz) const { return spectrumDb (hz); }   // spektrum bez sklonu (pre asistenta)
    virtual bool spectrumPostEq() const { return true; }                       // spektrum je az za EQ
};

class EqView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit EqView (EqModel& m) : model (m)
    {
        asst.setButtonText (juce::String::fromUTF8 ("✦ Asistent"));
        asst.onClick = [this] { asstMenu(); };
        addAndMakeVisible (asst);
        sila.setSliderStyle (juce::Slider::LinearHorizontal); sila.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sila.setRange (0, 100, 1); sila.setValue (80, juce::dontSendNotification);
        sila.onValueChange = [this] { applySuggestion(); };
        back.setButtonText (juce::String::fromUTF8 ("Späť")); back.onClick = [this] { restore(); };
        addChildComponent (sila); addChildComponent (back);
    }
    ~EqView() override { stopTimer(); }
    std::function<juce::String (const juce::String&)> tr = [] (const juce::String& s) { return s; };
    juce::Colour accent { 0xffc9a35a };
    float dbRange = 18.0f;

    static juce::Colour bandColour (int i)
    {
        static const juce::uint32 c[8] = { 0xff8a9bb0, 0xff6f9fb8, 0xffc9a35a, 0xff8fb59a, 0xffc98f7a, 0xffa58fc4, 0xff7fb4b0, 0xffb0a07a };
        return juce::Colour (c[(size_t) juce::jlimit (0, 7, i)]);
    }
    void refresh() { repaint(); }
    void startListening (int prof)
    {
        profile = prof; heard = 0; for (auto& v : acc) v = 0; for (auto& v : accF) v = 0;
        listening = true; asst.setButtonText (juce::String::fromUTF8 ("■ Zastaviť")); startTimerHz (25); repaint();
    }
    void stepForTest() { timerCallback(); }
    // novy ciel EQ (ina vrstva / efekt): zrusi rozpracovany navrh asistenta
    void resetAssistant()
    {
        stopTimer(); listening = false; sug.clear(); saved.clear();
        sila.setVisible (false); back.setVisible (false);
        asst.setButtonText (juce::String::fromUTF8 ("✦ Asistent")); asst.setVisible (model.numBands() >= 8);
        repaint();
    }
    void resized() override
    {
        const auto a = area().toNearestInt();
        asst.setBounds (a.getX() + 4, a.getY() + 3, 92, 20);
        sila.setBounds (a.getX() + 140, a.getY() + 3, 110, 20);
        back.setBounds (a.getX() + 254, a.getY() + 3, 54, 20);
    }
    int selected() const { return sel; }

    void paint (juce::Graphics& g) override
    {
        const auto a = area();
        g.setColour (juce::Colour (0xff161c25)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setFont (juce::FontOptions (10.0f));
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xOf (hz);
            g.setColour (juce::Colour (0xff232b37)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (juce::Colour (0xff6d7a8c));
            g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), (int) x - 15, (int) a.getBottom() + 1, 30, 12, juce::Justification::centred);
        }
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            g.setColour (db == 0 ? juce::Colour (0xff3a4656) : juce::Colour (0xff232b37)); g.drawHorizontalLine ((int) yOf (db), a.getX(), a.getRight());
            g.setColour (juce::Colour (0xff6d7a8c));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), 0, (int) yOf (db) - 6, (int) a.getX() - 3, 12, juce::Justification::centredRight);
        }
        // spektrum zvuku
        if (model.spectrumDb (1000.0f) > -199.0f)
        {
            juce::Path sp; sp.startNewSubPath (a.getX(), a.getBottom());
            for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
            {
                const float v = juce::jlimit (-90.0f, 0.0f, model.spectrumDb (hzOf (x)));
                sp.lineTo (x, juce::jmap (v, -90.0f, 0.0f, a.getBottom(), a.getY()));
            }
            sp.lineTo (a.getRight(), a.getBottom()); sp.closeSubPath();
            const juce::Colour sc (0xff4f7d8c);
            g.setGradientFill (juce::ColourGradient (sc.withAlpha (0.45f), 0, a.getY(), sc.withAlpha (0.08f), 0, a.getBottom(), false));
            g.fillPath (sp);
        }
        const int n = model.numBands();
        // tvar kazdeho aktivneho pasma (jemne, vo farbe pasma); vybrane pasmo vyraznejsie
        for (int i = 0; i < n; ++i)
        {
            const auto b = model.band (i);
            const bool isSel = i == sel || i == drag;
            if (! b.active && ! isSel) continue;
            const bool ghost = ! b.active || (b.hasG && std::abs (b.g) < 0.05f);   // nulove pasmo: nahlad tvaru pri +6 dB, aby bolo vidno sirku
            juce::Path p; const float y0 = yOf (0);
            p.startNewSubPath (a.getX(), y0);
            for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
            {
                const double m = ghost && b.hasG ? model.bandMagnitudeAt (i, hzOf (x), 6.0f) : model.bandMagnitude (i, hzOf (x));
                p.lineTo (x, juce::jlimit (a.getY(), a.getBottom(), yOf ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, m))))));
            }
            p.lineTo (a.getRight(), y0); p.closeSubPath();
            const auto c = bandColour (i);
            if (ghost) { g.setColour (c.withAlpha (isSel ? 0.5f : 0.25f)); g.strokePath (p, juce::PathStrokeType (1.0f), {}); }
            else { g.setColour (c.withAlpha (isSel ? 0.30f : 0.12f)); g.fillPath (p); }
        }
        // celkova krivka
        {
            juce::Path cv;
            for (float x = a.getX(); x <= a.getRight(); x += 1.5f)
            {
                const float y = juce::jlimit (a.getY(), a.getBottom(), yOf ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, model.magnitude (hzOf (x)))))));
                if (x == a.getX()) cv.startNewSubPath (x, y); else cv.lineTo (x, y);
            }
            g.setColour (model.enabled() ? accent : accent.withAlpha (0.35f)); g.strokePath (cv, juce::PathStrokeType (2.2f));
        }
        // uchytky sirky vybraneho pasma
        if (sel >= 0 && sel < n && model.band (sel).hasQ)
        {
            const auto b = model.band (sel); const auto [l, r] = edges (b);
            const float yh = handleY (b);
            g.setColour (bandColour (sel).withAlpha (0.8f));
            g.drawLine (xOf (l), yh, xOf (r), yh, 1.0f);
            for (float hz : { l, r })
            {
                const float x = xOf (hz);
                g.fillRoundedRectangle (x - 3, yh - 9, 6, 18, 2.0f);
            }
        }
        // body pasiem
        for (int i = 0; i < n; ++i)
        {
            const auto b = model.band (i); const auto c = pos (i);
            const bool isSel = i == sel || i == drag, hov = i == hover;
            const float r = isSel ? 8.5f : 7.5f;
            g.setColour (b.active ? bandColour (i) : bandColour (i).withAlpha (0.45f));
            if (b.type >= 3) g.fillRoundedRectangle (c.x - r + 1, c.y - r + 1, 2 * r - 2, 2 * r - 2, 2.0f); else g.fillRoundedRectangle (c.x - r, c.y - r, 2 * r, 2 * r, 2.0f);
            if (isSel || hov) { g.setColour (juce::Colours::white.withAlpha (isSel ? 0.9f : 0.5f)); g.drawRoundedRectangle (c.x - r - 2, c.y - r - 2, 2 * r + 4, 2 * r + 4, 2.0f, 1.3f); }
            g.setColour (juce::Colour (0xff161c25)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), juce::Justification::centred);
        }
        if (listening)
        {
            auto box = juce::Rectangle<float> (a.getCentreX() - 170, a.getCentreY() - 26, 340, 52);
            g.setColour (juce::Colour (0xe0161c25)); g.fillRoundedRectangle (box, 3.0f);
            g.setColour (juce::Colour (0xffe4e9ef)); g.setFont (juce::FontOptions (13.0f));
            g.drawText (tr (juce::String::fromUTF8 (heard < 1 ? "Asistent počúva – pusti skladbu…" : "Asistent počúva…")) + "  " + juce::String (juce::roundToInt (100.0f * (float) heard / (float) needFrames)) + " %",
                        box.withHeight (26).reduced (8, 0), juce::Justification::centred);
            const auto bar = box.removeFromBottom (18).reduced (14, 5);
            g.setColour (juce::Colour (0xff232b37)); g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) heard / (float) needFrames), 2.0f);
        }
        if (sila.isVisible())
        {
            g.setColour (juce::Colour (0xffb7c2cf)); g.setFont (juce::FontOptions (11.0f));
            g.drawText (tr (juce::String::fromUTF8 ("Sila")) + " " + juce::String ((int) sila.getValue()) + " %", sila.getX() - 44, sila.getY(), 42, 20, juce::Justification::centredRight);
        }
        // hodnoty vybraneho / tahaneho pasma
        const int k = drag >= 0 ? drag : (hover >= 0 ? hover : sel);
        if (k >= 0 && k < n)
        {
            const auto b = model.band (k);
            juce::String t = juce::String (k + 1) + "  " + typeName (b.type) + "   " + hzText (b.f);
            if ((b.type == 3 && b.f < 20.5f) || (b.type == 4 && b.f > 19900.0f)) t = juce::String (k + 1) + "  " + typeName (b.type) + "   " + tr (juce::String::fromUTF8 ("vypnuté"));
            else
            {
                if (b.hasG) t << "   " << (b.g > 0 ? "+" : "") << juce::String (b.g, 1) << " dB";
                if (b.hasQ) t << "   Q " << juce::String (b.q, 2);
            }
            g.setFont (juce::FontOptions (12.0f));
            const auto box = juce::Rectangle<float> (a.getRight() - 260, a.getY() + 3, 256, 18);
            g.setColour (juce::Colour (0xcc161c25)); g.fillRoundedRectangle (box, 3.0f);
            g.setColour (bandColour (k).brighter (0.3f)); g.drawText (t, box.reduced (6, 0), juce::Justification::centredRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = hitPoint (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        mode = None; drag = -1;
        const int hd = hitHandle (e.position);
        if (hd >= 0) { drag = sel; mode = Width; model.begin (drag); repaint(); return; }
        const int k = hitPoint (e.position);
        if (k < 0) { sel = -1; repaint(); return; }
        sel = drag = k;
        const auto b = model.band (k);
        start = e.position; f0 = b.f; g0 = b.g; q0 = b.q;
        // pravym tlacidlom (alebo Alt) + tahanie hore / dole = sirka pasma; inak frekvencia a zisk, Shift = jemne
        mode = (b.hasQ && (e.mods.isRightButtonDown() || e.mods.isAltDown())) ? QDrag : Move;
        model.begin (k);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const auto b = model.band (drag);
        if (mode == Width)
        {
            const float hz = hzOf (e.position.x);
            const float bw = juce::jlimit (0.05f, 6.0f, 2.0f * std::abs (std::log2 (hz / b.f)));
            model.setQ (drag, juce::jlimit (b.qMin, b.qMax, qFromBw (bw)));
        }
        else if (mode == QDrag)
            model.setQ (drag, juce::jlimit (b.qMin, b.qMax, q0 * std::pow (2.0f, (start.y - e.position.y) / 40.0f)));
        else if (mode == Move)
        {
            const float k = e.mods.isShiftDown() ? 0.15f : 1.0f;
            const auto p = start + (e.position - start) * k;
            if (b.hasF && b.fMin < b.fMax) model.setF (drag, juce::jlimit (b.fMin, b.fMax, hzOf (xOf (f0) + (p.x - start.x))));
            if (b.hasG) model.setG (drag, juce::jlimit (-15.0f, 15.0f, std::round (dbOf (yOf (g0) + (p.y - start.y)) * 10.0f) / 10.0f));
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (drag >= 0) model.end (drag); drag = -1; mode = None; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int k = hitPoint (e.position); if (k < 0) return;
        const auto b = model.band (k);
        model.begin (k);
        if (b.hasG) model.setG (k, 0.0f);
        if (b.hasF && b.fMin < b.fMax) model.setF (k, b.fDef);
        if (b.hasQ) model.setQ (k, b.qDef);
        model.end (k);
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int k = hitPoint (e.position); if (k < 0 && hitHandle (e.position) >= 0) k = sel;
        if (k < 0) return;
        const auto b = model.band (k); if (! b.hasQ) return;
        sel = k;
        model.begin (k); model.setQ (k, juce::jlimit (b.qMin, b.qMax, b.q * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f))); model.end (k);
        repaint();
    }

private:
    // ---------------------------------------------------------------- asistent: pocuva hudbu a navrhne EQ
    static constexpr int NB = 31;   // tretinooktavove pasma 20 Hz - 20 kHz
    static float centre (int k) { return 20.0f * std::pow (2.0f, (float) k / 3.0f); }
    juce::TextButton asst, back; juce::Slider sila;
    bool listening = false; int heard = 0, needFrames = 300, profile = 0;
    double acc[NB] {};
    static constexpr int NF = 120;   // jemna mriezka 1/12 oktavy (na rezonancie)
    static float fine (int k) { return 20.0f * std::pow (2.0f, (float) k / 12.0f); }
    double accF[NF] {};
    struct Sug { int band; float f, g, q; };
    std::vector<Sug> sug; std::vector<EqBand> saved;
    void asstMenu()
    {
        if (listening) { stopTimer(); listening = false; asst.setButtonText (juce::String::fromUTF8 ("✦ Asistent")); repaint(); return; }
        if (model.numBands() < 8) return;
        juce::PopupMenu m;
        static const char* names[] = { "Celá pieseň / mix", "Spev", "Gitara / klavír", "Basa", "Bicie", "Syntezátor / pad" };
        for (int i = 0; i < 6; ++i) m.addItem (i + 1, tr (juce::String::fromUTF8 (names[i])));
        if (eqReference().valid.load()) { m.addSeparator(); m.addItem (7, tr (juce::String::fromUTF8 ("Podľa referenčnej pesničky"))); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (asst), [this] (int r)
        {
            if (r <= 0) return;
            startListening (r - 1);
        });
    }
    void timerCallback() override
    {
        if (! listening) { stopTimer(); return; }
        float lvl[NB]; float loud = -200.0f;
        for (int k = 0; k < NB; ++k)
        {   // priemer troch bodov v tretinooktave
            float s = 0; for (int j = -1; j <= 1; ++j) s += std::pow (10.0f, model.spectrumDbRaw (centre (k) * std::pow (2.0f, (float) j / 9.0f)) / 10.0f);
            lvl[k] = 10.0f * std::log10 (juce::jmax (1.0e-12f, s / 3.0f)); loud = juce::jmax (loud, lvl[k]);
        }
        if (loud > -70.0f)
        {   // len ked hra zvuk
            for (int k = 0; k < NB; ++k) acc[k] += std::pow (10.0, lvl[k] / 10.0);
            for (int k = 0; k < NF; ++k) accF[k] += std::pow (10.0, model.spectrumDbRaw (fine (k)) / 10.0);
            ++heard;
        }
        if (heard >= needFrames) { stopTimer(); listening = false; asst.setButtonText (juce::String::fromUTF8 ("✦ Asistent")); computeSuggestion(); }
        repaint();
    }
    // cielovy priebeh (dB) podla druhu zvuku - dlhodoby priemer vyvazenej nahravky
    float target (float hz) const
    {
        const float oct = std::log2 (hz / 1000.0f);
        float slope = -4.5f, lowKnee = 60.0f, hiKnee = 14000.0f;
        switch (profile)
        {
            case 1: slope = -4.0f; lowKnee = 110.0f; hiKnee = 13000.0f; break;   // spev
            case 2: slope = -4.5f; lowKnee = 80.0f; break;                       // gitara / klavir
            case 3: slope = -7.0f; lowKnee = 40.0f; hiKnee = 6000.0f; break;     // basa
            case 4: slope = -3.0f; lowKnee = 45.0f; break;                       // bicie
            case 5: slope = -4.0f; lowKnee = 50.0f; break;                       // syntezator
            default: break;                                                      // mix
        }
        float t = slope * oct;
        if (hz < lowKnee) t -= 12.0f * std::log2 (lowKnee / hz);    // pod kolenom basy klesaju
        if (hz > hiKnee) t -= 10.0f * std::log2 (hz / hiKnee);
        return t;
    }
    void computeSuggestion()
    {
        if (heard < 10) return;
        float M[NB], D[NB], Ds[NB];
        for (int k = 0; k < NB; ++k)
        {
            M[k] = 10.0f * (float) std::log10 (juce::jmax (1.0e-12, acc[k] / heard));
            if (model.spectrumPostEq()) M[k] -= 20.0f * (float) std::log10 (juce::jmax (1.0e-6, model.magnitude (centre (k))));   // bez sucasneho EQ
            D[k] = M[k] - (profile == 6 ? eqReference().db[k] : target (centre (k)));
        }
        // uroven nezavisla: odcitat priemer v 100 Hz - 8 kHz
        float mean = 0; int cnt = 0; for (int k = 0; k < NB; ++k) if (centre (k) >= 100 && centre (k) <= 8000) { mean += D[k]; ++cnt; }
        mean /= (float) juce::jmax (1, cnt); for (auto& d : D) d -= mean;
        for (int k = 0; k < NB; ++k) { float s = 0, w = 0; for (int j = -1; j <= 1; ++j) { const int i = juce::jlimit (0, NB - 1, k + j); const float ww = j == 0 ? 2.0f : 1.0f; s += D[i] * ww; w += ww; } Ds[k] = s / w; }
        auto avg = [&] (float lo, float hi) { float s = 0; int c = 0; for (int k = 0; k < NB; ++k) if (centre (k) >= lo && centre (k) <= hi) { s += Ds[k]; ++c; } return c ? s / (float) c : 0.0f; };
        auto clampG = [] (float g) { return juce::jlimit (-5.0f, 5.0f, g); };
        sug.clear();
        // 1 orezanie basov: spev vzdy, ostatne ak je hlboko vela zbytocnej energie
        if (profile == 1) sug.push_back ({ 0, 90.0f, 0, 0.707f });
        else if (profile != 3 && avg (20, 35) > 6.0f) sug.push_back ({ 0, 30.0f, 0, 0.707f });
        else sug.push_back ({ 0, 20.0f, 0, 0.707f });
        // 2 basy a 7 vysky (shelfy)
        sug.push_back ({ 1, 120.0f, clampG (-avg (45, 140)), 0.7f });
        sug.push_back ({ 6, profile == 3 ? 4000.0f : 8000.0f, clampG (-avg (profile == 3 ? 3000 : 6000, profile == 3 ? 6000 : 14000)), 0.7f });
        // 3-5 spicky: najvacsie odchylky 160 Hz - 6 kHz, aspon oktavu od seba
        std::vector<int> used;
        for (int n = 0; n < 3; ++n)
        {
            int best = -1; float bv = 1.0f;
            for (int k = 0; k < NB; ++k)
            {
                const float f = centre (k); if (f < 160 || f > 6000) continue;
                bool near = false; for (int u : used) if (std::abs (u - k) < 3) near = true; if (near) continue;
                if (std::abs (Ds[k]) > bv) { bv = std::abs (Ds[k]); best = k; }
            }
            if (best < 0) break;
            used.push_back (best);
            int w = 0; while (w < 4 && best - w - 1 >= 0 && best + w + 1 < NB && std::abs (Ds[best - w - 1]) > bv * 0.5f && std::abs (Ds[best + w + 1]) > bv * 0.5f) ++w;
            const float q = juce::jlimit (0.6f, 2.5f, 1.4f / (float) (w + 1) * 1.5f);
            sug.push_back ({ -1, centre (best), clampG (-Ds[best]), q });
        }
        // rezonancia: uzky vrchol nad okolim (plechovy / pistiaci ton) - pasmo 6
        {   // jemna mriezka: vrchol o viac ako 3 dB nad okolim (+-1/2 oktavy)
            float F[NF]; for (int k = 0; k < NF; ++k) F[k] = 10.0f * (float) std::log10 (juce::jmax (1.0e-12, accF[k] / heard));
            int best = -1; float bv = 3.0f;
            for (int k = 6; k < NF - 6; ++k)
            {
                const float f = fine (k); if (f < 200 || f > 9000) continue;
                float s2 = 0; for (int j = -6; j <= 6; ++j) if (std::abs (j) > 1) s2 += F[k + j]; s2 /= 10.0f;
                const float r = F[k] - s2; if (r > bv && F[k] >= F[k - 1] && F[k] >= F[k + 1]) { bv = r; best = k; }
            }
            if (best >= 0) sug.push_back ({ 5, fine (best), juce::jlimit (-6.0f, -2.0f, -bv), 6.0f });
        }
        // spicky do pasiem 3, 4, 5 (pasmo 3 len 200 Hz - 8 kHz)
        std::vector<Sug> peaks; for (auto& x : sug) if (x.band == -1) peaks.push_back (x);
        sug.erase (std::remove_if (sug.begin(), sug.end(), [] (const Sug& x) { return x.band == -1; }), sug.end());
        std::sort (peaks.begin(), peaks.end(), [] (const Sug& a2, const Sug& b2) { return a2.f < b2.f; });
        int slots[3] = { 2, 3, 4 }; int si = 0;
        for (auto& pk : peaks) { if (si >= 3) break; pk.band = slots[si++]; sug.push_back (pk); }
        sug.push_back ({ 7, 20000.0f, 0, 0.707f });
        // ulozit povodny stav (pre Spat) a pouzit navrh
        saved.clear(); for (int i = 0; i < model.numBands(); ++i) saved.push_back (model.band (i));
        sila.setValue (80, juce::dontSendNotification); sila.setVisible (true); back.setVisible (true);
        applySuggestion();
    }
    void applySuggestion()
    {
        if (sug.empty() || saved.empty()) return;
        const float s = (float) sila.getValue() / 100.0f;
        for (int i = 0; i < model.numBands(); ++i)
        {   // pasma bez navrhu na nulu
            bool has = false; for (auto& x : sug) if (x.band == i) has = true;
            if (has) continue; const auto b = model.band (i);
            model.begin (i); if (b.hasG) model.setG (i, 0.0f); model.end (i);
        }
        for (auto& x : sug)
        {
            const auto b = model.band (x.band);
            model.begin (x.band);
            if (b.hasF) model.setF (x.band, juce::jlimit (b.fMin, b.fMax, x.f));
            if (b.hasG) model.setG (x.band, std::round (x.g * s * 10.0f) / 10.0f);
            if (b.hasQ) model.setQ (x.band, juce::jlimit (b.qMin, b.qMax, x.q));
            model.end (x.band);
        }
        repaint();
    }
    void restore()
    {
        for (int i = 0; i < (int) saved.size() && i < model.numBands(); ++i)
        {
            const auto& b = saved[(size_t) i]; model.begin (i);
            if (b.hasF) model.setF (i, b.f); if (b.hasG) model.setG (i, b.g); if (b.hasQ) model.setQ (i, b.q);
            model.end (i);
        }
        sug.clear(); saved.clear(); sila.setVisible (false); back.setVisible (false); repaint();
    }
    enum Mode { None, Move, QDrag, Width };
    EqModel& model;
    int hover = -1, drag = -1, sel = -1; Mode mode = None;
    juce::Point<float> start; float f0 = 1000, g0 = 0, q0 = 1;

    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().withTrimmedLeft (26).withTrimmedRight (6).withTrimmedTop (6).withTrimmedBottom (16); }
    float xOf (float hz) const { const auto a = area(); return a.getX() + a.getWidth() * std::log (juce::jmax (1.0f, hz) / 20.0f) / std::log (1000.0f); }
    float hzOf (float x) const { const auto a = area(); return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth())); }
    float yOf (float db) const { const auto a = area(); return juce::jmap (juce::jlimit (-dbRange, dbRange, db), dbRange, -dbRange, a.getY(), a.getBottom()); }
    float dbOf (float y) const { const auto a = area(); return juce::jmap (y, a.getY(), a.getBottom(), dbRange, -dbRange); }
    static float bwFromQ (float q) { return 2.0f * std::asinh (1.0f / (2.0f * q)) / std::log (2.0f); }
    static float qFromBw (float bw) { return 1.0f / (2.0f * std::sinh (std::log (2.0f) * bw * 0.5f)); }
    static std::pair<float, float> edges (const EqBand& b) { const float h = bwFromQ (b.q) * 0.5f; return { b.f * std::pow (2.0f, -h), b.f * std::pow (2.0f, h) }; }
    float handleY (const EqBand& b) const { return yOf (std::abs (b.g) < 0.05f ? 3.0f : b.g * 0.5f); }
    juce::Point<float> pos (int i) const
    {
        const auto b = model.band (i);
        if (b.hasG) return { xOf (b.f), yOf (b.g) };
        return { xOf (b.f), yOf ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, model.magnitude (b.f))))) };
    }
    int hitPoint (juce::Point<float> p) const
    {
        int best = -1; float bd = 15.0f;
        for (int i = 0; i < model.numBands(); ++i) { float d = pos (i).getDistanceFrom (p); if (i == sel) d -= 3.0f; if (d < bd) { bd = d; best = i; } }
        return best;
    }
    int hitHandle (juce::Point<float> p) const
    {
        if (sel < 0 || sel >= model.numBands()) return -1;
        const auto b = model.band (sel); if (! b.hasQ) return -1;
        const auto [l, r] = edges (b); const float yh = handleY (b);
        for (int s = 0; s < 2; ++s) { const float x = xOf (s == 0 ? l : r); if (std::abs (p.x - x) < 7.0f && std::abs (p.y - yh) < 12.0f) return s; }
        return -1;
    }
    juce::String typeName (int t) const
    {
        static const char* n[5] = { "Basy", "Pásmo", "Výšky", "Orezanie basov", "Orezanie výšok" };
        return tr (juce::String::fromUTF8 (n[juce::jlimit (0, 4, t)]));
    }
    static juce::String hzText (float f) { return f >= 1000 ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
};
} // namespace korgus
