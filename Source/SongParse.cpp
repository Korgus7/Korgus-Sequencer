#include "SongParse.h"

static const int GM_DRUMS[] = { 36, 38, 39, 42, 46, 45, 50, 37, 49, 56 };

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
            if (const auto& ar = t["arp"]; ar.isObject())
            {
                td.arp.on = (bool) ar.getProperty ("on", false);
                td.arp.rate = juce::jlimit (0, 4, (int) ar.getProperty ("rate", 3));
                td.arp.mode = juce::jlimit (0, 4, (int) ar.getProperty ("mode", 0));
                td.arp.oct = juce::jlimit (1, 3, (int) ar.getProperty ("oct", 1));
                td.arp.gate = juce::jlimit (0.05f, 1.0f, (float) (double) ar.getProperty ("gate", 0.8));
            }
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
            td.audible = ! (bool) t.getProperty ("mute", false) && (! anySolo || (bool) t.getProperty ("solo", false));

            std::array<AudioBufPtr, 10> laneBuf {};
            if (type == "drum")
                if (auto* sm = t["smp"].getDynamicObject())
                    for (const auto& prop : sm->getProperties())
                    {
                        const int l = prop.name.toString().getIntValue();
                        if (juce::isPositiveAndBelow (l, 10)) laneBuf[(size_t) l] = clipFor (prop.value.toString(), 1.0, 0);
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
                            for (int l = 0; l < juce::jmin (10, grid->size()); ++l)
                            {
                                const auto* row = (*grid)[l].getArray();
                                if (row == nullptr || idx >= row->size()) continue;
                                const int val = (int) (*row)[idx];
                                if (val == 0) continue;
                                const double at = a + st + ((st % 2) ? swing : 0.0);
                                const float vel = val == 2 ? 1.0f : 0.62f;
                                if (laneBuf[(size_t) l] != nullptr)
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
