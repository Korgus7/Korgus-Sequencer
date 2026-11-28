#include <map>
#include "SongParse.h"

static const int GM_DRUMS[] = { 36, 38, 39, 42, 46, 45, 50, 37, 49, 56, 51, 44, 47, 41, 43, 57 };   // 16 riadkov bicich (rovnako ako DRUMS v rozhrani)
static constexpr int kDrumLanes = 16;

namespace
{
    // akordovy symbol -> MIDI noty (rovnako ako parseChord / chordPitches v index.html: bas + akord v strednej polohe)
    std::vector<int> chordNotes (const juce::String& txt)
    {
        const auto t = txt.trim();
        if (t.isEmpty()) return {};
        auto pc = [] (juce::juce_wchar l, juce::juce_wchar acc) -> int
        {
            int v;
            switch (juce::CharacterFunctions::toUpperCase (l))
            {
                case 'C': v = 0; break;  case 'D': v = 2; break;  case 'E': v = 4; break;  case 'F': v = 5; break;
                case 'G': v = 7; break;  case 'A': v = 9; break;  case 'H': case 'B': v = 11; break;
                default: return -1;
            }
            if (acc == '#') ++v;
            if (acc == 'b') --v;
            return (v + 12) % 12;
        };
        int i = 0;
        const int root = pc (t[i++], 0);
        if (root < 0) return {};
        int rootAcc = 0;
        if (t[i] == '#' || t[i] == 'b') { rootAcc = t[i] == '#' ? 1 : -1; ++i; }
        const int rt = (root + rootAcc + 12) % 12;
        auto rest = t.substring (i);
        int bass = -1;
        if (const int sl = rest.indexOfChar ('/'); sl >= 0)
        {
            const auto b = rest.substring (sl + 1).trim();
            if (b.isNotEmpty()) bass = pc (b[0], b.length() > 1 ? b[1] : 0);
            rest = rest.substring (0, sl);
        }
        const auto q = rest.toLowerCase().removeCharacters (" \t");
        auto has = [&q] (const char* x) { return q.contains (x); };
        std::vector<int> iv;
        if (q.startsWith ("maj7") || q == "m7+") iv = { 0, 4, 7, 11 };
        else if (q.startsWith ("m7b5")) iv = { 0, 3, 6, 10 };
        else if (q.startsWith ("dim") || q == "o") iv = has ("7") ? std::vector<int> { 0, 3, 6, 9 } : std::vector<int> { 0, 3, 6 };
        else if (q.startsWith ("aug") || q == "+") iv = { 0, 4, 8 };
        else if (q.startsWith ("sus2")) iv = { 0, 2, 7 };
        else if (q.startsWith ("sus")) iv = { 0, 5, 7 };
        else if (q.startsWith ("m") && ! q.startsWith ("maj"))
        {
            iv = { 0, 3, 7 };
            if (has ("7")) iv.push_back (10); else if (has ("6")) iv.push_back (9);
            if (has ("9")) iv.push_back (14);
        }
        else
        {
            iv = { 0, 4, 7 };
            if (has ("7")) iv.push_back (10); else if (has ("6")) iv.push_back (9);
            if (has ("9")) iv.push_back (14);
        }
        if (has ("add9") && std::find (iv.begin(), iv.end(), 14) == iv.end()) iv.push_back (14);
        const int base = 48 + rt + (rt > 7 ? -12 : 0);
        std::vector<int> ps;
        ps.push_back (bass >= 0 ? 36 + bass + (bass > 7 ? -12 : 0) + 12 : base);
        for (int x : iv) ps.push_back (base + 12 + x);
        std::sort (ps.begin(), ps.end());
        ps.erase (std::unique (ps.begin(), ps.end()), ps.end());
        ps.erase (std::remove_if (ps.begin(), ps.end(), [] (int x) { return x < 0 || x > 127; }), ps.end());
        return ps;
    }
}

std::unique_ptr<SongData> parseSong (const juce::var& v, const std::function<int (const juce::String&)>& slotFor,
                                     const std::function<AudioBufPtr (const juce::String&, double, int)>& clipFor)
{
    const juce::var j = v.isString() ? juce::JSON::parse (v.toString()) : v;
    if (! j.isObject()) return nullptr;

    auto sd = std::make_unique<SongData>();
    sd->tempo   = juce::jlimit (20.0, 400.0, (double) j.getProperty ("tempo", 120));
    if (const auto* tm = j["tempoMap"].getArray(); tm != nullptr && ! tm->isEmpty())
    {
        sd->tempoPts.push_back ({ 0.0, sd->tempo });
        for (const auto& q : *tm)
            if (const auto* xy = q.getArray(); xy != nullptr && xy->size() >= 2)
            {
                const double st = juce::jmax (0.0, (double) (*xy)[0]), bpm = juce::jlimit (20.0, 400.0, (double) (*xy)[1]);
                if (st <= 0.0) sd->tempoPts.front().second = bpm;
                else sd->tempoPts.push_back ({ st, bpm });
            }
        std::sort (sd->tempoPts.begin(), sd->tempoPts.end());
        if (sd->tempoPts.size() < 2) sd->tempoPts.clear();
    }
    sd->endStep = juce::jmax (16.0, (double) j.getProperty ("bars", 32) * 16.0);
    sd->locL    = (double) j.getProperty ("locL", 0);
    sd->locR    = (double) j.getProperty ("locR", 64);
    sd->loopOn  = (bool) j.getProperty ("loopOn", false);
    sd->click   = (bool) j.getProperty ("click", false);
    { const float mv = (float) (double) j.getProperty ("masterVol", 1.0); sd->masterGain = mv * mv; }
    if (const int kk = (int) j.getProperty ("keyK", -1); kk >= 0 && kk < 12)
    {
        sd->keyTonic = kk;
        sd->keyScale = (int) j.getProperty ("keyM", 0) == 1 ? 1 : 0;
    }
    if (const auto* ch = j["chords"].getArray())
    {
        for (const auto& c : *ch)
        {
            auto notes = chordNotes (c["c"].toString());
            if (! notes.empty()) sd->chords.push_back ({ juce::jmax (0.0, (double) c["s"]), std::move (notes) });
        }
        std::stable_sort (sd->chords.begin(), sd->chords.end(), [] (const auto& a, const auto& b) { return a.step < b.step; });
    }
    if (const auto* aux = j["aux"].getArray())
        for (int k = 0; k < juce::jmin (2, aux->size()); ++k)
        {
            const float av = juce::jlimit (0.0f, 1.0f, (float) (double) (*aux)[k].getProperty ("vol", 1.0));
            sd->auxGain[k] = av * av;
        }
    auto offBits = [&j] (const juce::String& id) -> unsigned
    {
        unsigned b = 0;
        if (const auto* arr = j["byp"][juce::Identifier (id)].getArray())
            for (int k = 0; k < juce::jmin (4, arr->size()); ++k) if ((bool) (*arr)[k]) b |= 1u << k;
        return b;
    };
    sd->masterFxOff = offBits ("__master");
    sd->auxFxOff[0] = offBits ("__aux1");
    sd->auxFxOff[1] = offBits ("__aux2");
    const double swing = (double) j.getProperty ("swing", 0) / 100.0 * 0.6;

    const auto* tracks = j["tracks"].getArray();
    bool anySolo = false;
    // skupinove stopy (zbernice)
    std::map<juce::String, int> groupIdx;
    juce::StringArray soloGroups;
    if (const auto* groups = j["groups"].getArray())
        for (const auto& g : *groups)
        {
            const auto gid = g["id"].toString();
            if (gid.isEmpty()) continue;
            GroupData gd;
            gd.slot = slotFor ("__g" + gid);
            const float vol = (float) (double) g.getProperty ("vol", 1.0);
            const float pan = juce::jlimit (-1.0f, 1.0f, (float) (double) g.getProperty ("pan", 0.0));
            gd.gainL = vol * vol * juce::jmin (1.0f, 1.0f - pan);
            gd.gainR = vol * vol * juce::jmin (1.0f, 1.0f + pan);
            gd.audible = ! (bool) g.getProperty ("mute", false);
            gd.fxOff = offBits ("__g" + gid);
            if (const auto* snd = g["send"].getArray())
                for (int k = 0; k < juce::jmin (2, snd->size()); ++k) { const float sv = juce::jlimit (0.0f, 1.0f, (float) (double) (*snd)[k]); gd.send[k] = sv * sv; }
            if ((bool) g.getProperty ("solo", false)) { soloGroups.add (gid); anySolo = true; }
            groupIdx[gid] = (int) sd->groups.size();
            sd->groups.push_back (gd);
        }
    if (tracks != nullptr)
        for (const auto& t : *tracks) anySolo = anySolo || (bool) t.getProperty ("solo", false);

    if (tracks != nullptr)
    {
        for (const auto& t : *tracks)
        {
            const auto type = t["type"].toString();
            TrackData td;
            td.isAudio = type == "audio";
            td.slot = slotFor (t["id"].toString());
            td.channel = juce::jlimit (1, 16, (int) t.getProperty ("midiCh", 1));
            td.midiOutCh = juce::jlimit (0, 16, (int) t.getProperty ("mpcCh", 0));
            td.isDrum = type == "drum";
            if ((bool) t.getProperty ("mpc", false))
            {   // stopa MPC: navrat zvuku z druheho vstupu ("a+b")
                const auto ret = t.getProperty ("ret", "").toString();
                if (ret.isNotEmpty())
                {
                    td.retA = ret.upToFirstOccurrenceOf ("+", false, false).getIntValue();
                    td.retB = ret.contains ("+") ? ret.fromFirstOccurrenceOf ("+", false, false).getIntValue() : -1;
                }
                td.monitor = (bool) t.getProperty ("mon", true);
            }
            if (td.isAudio)
            {
                td.monitor = (bool) t.getProperty ("mon", false);
                const auto inp = t.getProperty ("inp", "0").toString();
                td.inA = inp.upToFirstOccurrenceOf ("+", false, false).getIntValue();
                td.inB = inp.contains ("+") ? inp.fromFirstOccurrenceOf ("+", false, false).getIntValue() : -1;
            }
            const float vol = (float) (double) t.getProperty ("vol", 0.8);
            const float pan = juce::jlimit (-1.0f, 1.0f, (float) (double) t.getProperty ("pan", 0.0));
            const float g = vol * vol;
            td.gainL = g * juce::jmin (1.0f, 1.0f - pan);
            td.gainR = g * juce::jmin (1.0f, 1.0f + pan);
            td.vol = vol; td.pan = pan;
            td.isKey = (bool) t.getProperty ("sc", false);
            td.fxOff = offBits (t["id"].toString());
            if (const auto* snd = t["send"].getArray())
                for (int k = 0; k < juce::jmin (2, snd->size()); ++k)
                {
                    const float sv = juce::jlimit (0.0f, 1.0f, (float) (double) (*snd)[k]);
                    td.send[k] = sv * sv;
                }
            if (const auto* autos = t["auto"].getArray())
                for (const auto& av : *autos)
                {
                    AutoLane al;
                    const auto tt = av["t"].toString();
                    al.type = tt == "vol" ? 0 : tt == "pan" ? 1 : 2;
                    al.which = (int) av.getProperty ("w", 0);
                    al.param = (int) av.getProperty ("i", 0);
                    if (const auto* pts = av["pts"].getArray())
                        for (const auto& pv : *pts)
                            if (const auto* xy = pv.getArray(); xy != nullptr && xy->size() >= 2)
                                al.pts.push_back ({ (double) (*xy)[0], (float) (double) (*xy)[1] });
                    std::sort (al.pts.begin(), al.pts.end(), [] (const auto& x, const auto& y) { return x.first < y.first; });
                    if (! al.pts.empty()) td.autos.push_back (std::move (al));
                }
            {
                const auto out = t["out"].toString();
                auto gi = groupIdx.find (out);
                td.group = gi != groupIdx.end() ? gi->second : -1;
                td.audible = ! (bool) t.getProperty ("mute", false)
                          && (! anySolo || (bool) t.getProperty ("solo", false) || (out.isNotEmpty() && soloGroups.contains (out)));
            }

            // zmrazena stopa: namiesto nastroja sa hra nahrane audio (od zaciatku piesne)
            if (const auto fz = t["frozen"]; fz.isObject() && ! td.isAudio)
            {
                ClipEv c;
                c.start = 0; c.off = 0;
                c.dur = (double) fz.getProperty ("dur", 0.0);
                c.buf = clipFor (fz["aid"].toString(), 1.0, 0);
                if (c.buf != nullptr && c.dur > 0) { td.frozen = true; td.clips.push_back (c); sd->tracks.push_back (std::move (td)); continue; }
            }

            if (type == "drum")
                if (auto* ds = t["dset"].getDynamicObject())   // uprava zvuku nastrojov (editor samplu)
                    for (const auto& prop : ds->getProperties())
                    {
                        const int l = prop.name.toString().getIntValue();
                        if (! juce::isPositiveAndBelow (l, kDrumLanes)) continue;
                        const auto& o = prop.value;
                        auto f = [&o] (const char* k, float def) { return (float) (double) o.getProperty (k, def); };
                        auto& d = td.dset[(size_t) l];
                        d.on = true;
                        d.tr = f ("tr", 0); d.tune = f ("tune", 0); d.pEnv = f ("pEnv", 0); d.pA = f ("pA", 0); d.pH = f ("pH", 0); d.pD = f ("pD", 0.3f); d.pVel = f ("pVel", 0);
                        d.fOn = (bool) o.getProperty ("fOn", false); d.soft = (bool) o.getProperty ("soft", true); d.fType = (int) o.getProperty ("fType", 0);
                        d.cut = f ("cut", 20000); d.res = f ("res", 0); d.fEnv = f ("fEnv", 0); d.fA = f ("fA", 0); d.fH = f ("fH", 0); d.fD = f ("fD", 0.3f);
                        d.fVel = f ("fVel", 0); d.drive = f ("drive", 0); d.punch = f ("punch", 0);
                        d.gain = f ("gain", 0); d.pan = f ("pan", 0); d.aA = f ("aA", 0); d.aH = f ("aH", 2); d.aD = f ("aD", 4); d.aVel = f ("aVel", 1);
                    }
            std::array<AudioBufPtr, kDrumLanes> laneBuf {};
            if (type == "drum")
                if (auto* sm = t["smp"].getDynamicObject())
                    for (const auto& prop : sm->getProperties())
                    {
                        const int l = prop.name.toString().getIntValue();
                        if (juce::isPositiveAndBelow (l, kDrumLanes)) laneBuf[(size_t) l] = clipFor (prop.value.toString(), 1.0, 0);
                    }

            if (const auto* parts = t["parts"].getArray())
            {
                for (const auto& p : *parts)
                {
                    const double a = (double) p["start"] * 16.0, steps = (double) p["len"] * 16.0;
                    if (td.isAudio)
                    {
                        ClipEv c;
                        c.start = a;
                        // klip s povodnym tempom sa prisposobi tempu piesne (bez zmeny vysky tonu)
                        const double bpm = (double) p.getProperty ("bpm", 0.0);
                        double alpha = bpm > 0 ? bpm / sd->tempo : 1.0;
                        if (std::abs (alpha - 1.0) < 0.002 || alpha < 0.25 || alpha > 4.0) alpha = 1.0;
                        alpha = std::round (alpha * 10000.0) / 10000.0;
                        c.off = juce::jmax (0.0, (double) p.getProperty ("off", 0.0)) * alpha;
                        c.dur = (double) p.getProperty ("dur", 0.0) * alpha;
                        c.fi = juce::jmax (0.0, (double) p.getProperty ("fi", 0.0));
                        c.fo = juce::jmax (0.0, (double) p.getProperty ("fo", 0.0));
                        c.xfi = (bool) p.getProperty ("xfi", false); c.xfo = (bool) p.getProperty ("xfo", false);
                        c.gain = (float) (double) p.getProperty ("gain", 1.0);
                        c.buf = clipFor (p["aid"].toString(), alpha, juce::jlimit (-24, 24, (int) p.getProperty ("semi", 0)));
                        if (c.buf != nullptr && c.dur > 0) td.clips.push_back (c);
                    }
                    else if (type == "drum")
                    {
                        const int plen = juce::jmax (1, (int) p.getProperty ("plen", 16));
                        const auto* grid = p["grid"].getArray();
                        if (grid == nullptr) continue;
                        for (int st = 0; st < (int) steps; ++st)
                        {
                            const int idx = st % plen;
                            for (int l = 0; l < juce::jmin (kDrumLanes, grid->size()); ++l)
                            {
                                const auto* row = (*grid)[l].getArray();
                                if (row == nullptr || idx >= row->size()) continue;
                                const int val = (int) (*row)[idx];
                                if (val == 0) continue;
                                const double at = a + st + ((st % 2) ? swing : 0.0);
                                const float vel = val == 2 ? 1.0f : 0.62f;
                                if (laneBuf[(size_t) l] != nullptr && td.midiOutCh == 0)
                                {
                                    td.hits.push_back ({ at, laneBuf[(size_t) l], vel, l });
                                    continue;
                                }
                                NoteEv n;
                                n.start = at;
                                n.end = n.start + 0.9;
                                n.pitch = GM_DRUMS[l];
                                n.vel = vel;
                                td.notes.push_back (n);
                            }
                        }
                    }
                    else
                    {
                        if (auto* cc = p["cc"].getDynamicObject())
                            for (const auto& prop : cc->getProperties())
                            {
                                const auto key = prop.name.toString();
                                const bool pb = key == "pb";
                                const int ctrl = pb ? 128 : key.getIntValue();
                                if (! pb && ! juce::isPositiveAndBelow (ctrl, 128)) continue;
                                bool any = false;
                                if (const auto* pts = prop.value.getArray())
                                    for (const auto& q : *pts)
                                        if (const auto* xy = q.getArray(); xy != nullptr && xy->size() >= 2)
                                        {
                                            const double s = (double) (*xy)[0];
                                            if (s < 0 || s >= steps) continue;
                                            const int v = juce::jlimit (0, 127, (int) (*xy)[1]);
                                            td.ccs.push_back ({ a + s, ctrl, pb ? juce::jmin (16383, v * 128) : v });
                                            any = true;
                                        }
                                // na konci casti: sustain pusti, bend do stredu
                                if (any && (ctrl == 64 || pb)) td.ccs.push_back ({ a + steps - 0.01, ctrl, pb ? 8192 : 0 });
                            }
                    }
                    if (type != "drum" && ! td.isAudio)
                    if (const auto* notes = p["notes"].getArray())
                    {
                        for (const auto& nv : *notes)
                        {
                            const double s = (double) nv["s"];
                            if (s >= steps) continue;
                            NoteEv n;
                            const int si = (int) s;
                            n.start = a + s + ((si % 2) ? swing : 0.0);
                            n.end = a + juce::jmin (s + (double) nv["l"], steps) - 0.02;
                            n.pitch = juce::jlimit (0, 127, (int) nv["p"]);
                            n.vel = juce::jlimit (0.01f, 1.0f, (float) (double) nv["v"] / 127.0f);
                            td.notes.push_back (n);
                        }
                    }
                }
            }
            std::sort (td.notes.begin(), td.notes.end(), [] (const NoteEv& x, const NoteEv& y) { return x.start < y.start; });
            std::sort (td.hits.begin(), td.hits.end(), [] (const HitEv& x, const HitEv& y) { return x.start < y.start; });
            std::stable_sort (td.ccs.begin(), td.ccs.end(), [] (const CcEv& x, const CcEv& y) { return x.start < y.start; });
            sd->tracks.push_back (std::move (td));
        }
    }
    return sd;
}
