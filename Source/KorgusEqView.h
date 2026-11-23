#pragma once
// Spolocny graficky ekvalizer pre Korgus Sequencer (Korgus EQ) aj Korgus Inspiria.
// Vyzera aj ovlada sa vsade rovnako: body pasiem sa tahaju mysou, vybrane pasmo ma uchytky sirky.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>

namespace korgus
{
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
};

class EqView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EqView (EqModel& m) : model (m) {}
    std::function<juce::String (const juce::String&)> tr = [] (const juce::String& s) { return s; };
    juce::Colour accent { 0xffc9a35a };
    float dbRange = 18.0f;

    static juce::Colour bandColour (int i)
    {
        static const juce::uint32 c[8] = { 0xff8a9bb0, 0xff6f9fb8, 0xffc9a35a, 0xff8fb59a, 0xffc98f7a, 0xffa58fc4, 0xff7fb4b0, 0xffb0a07a };
        return juce::Colour (c[(size_t) juce::jlimit (0, 7, i)]);
    }
    void refresh() { repaint(); }
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
            if (b.type >= 3) g.fillRoundedRectangle (c.x - r + 1, c.y - r + 1, 2 * r - 2, 2 * r - 2, 2.0f); else g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
            if (isSel || hov) { g.setColour (juce::Colours::white.withAlpha (isSel ? 0.9f : 0.5f)); g.drawEllipse (c.x - r - 2, c.y - r - 2, 2 * r + 4, 2 * r + 4, 1.3f); }
            g.setColour (juce::Colour (0xff161c25)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), juce::Justification::centred);
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
