#include "SongParse.h"

static const int GM_DRUMS[] = { 36, 38, 39, 42, 46, 45, 50, 37, 49, 56 };

std::unique_ptr<SongData> parseSong (const juce::var& v, const std::function<int (const juce::String&)>& slotFor,
                                     const std::function<AudioBufPtr (const juce::String&)>& clipFor)
{
    const juce::var j = v.isString() ? juce::JSON::parse (v.toString()) : v;
    if (! j.isObject()) return nullptr;

    auto sd = std::make_unique<SongData>();
    sd->tempo   = juce::jlimit (20.0, 400.0, (double) j.getProperty ("tempo", 120));
    sd->endStep = juce::jmax (16.0, (double) j.getProperty ("bars", 32) * 16.0);
    sd->locL    = (double) j.getProperty ("locL", 0);
    sd->locR    = (double) j.getProperty ("locR", 64);
    sd->loopOn  = (bool) j.getProperty ("loopOn", false);
    sd->click   = (bool) j.getProperty ("click", false);
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
            td.audible = ! (bool) t.getProperty ("mute", false) && (! anySolo || (bool) t.getProperty ("solo", false));

            if (const auto* parts = t["parts"].getArray())
            {
                for (const auto& p : *parts)
                {
                    const double a = (double) p["start"] * 16.0, steps = (double) p["len"] * 16.0;
                    if (td.isAudio)
                    {
                        ClipEv c;
                        c.start = a;
                        c.off = juce::jmax (0.0, (double) p.getProperty ("off", 0.0));
                        c.dur = (double) p.getProperty ("dur", 0.0);
                        c.buf = clipFor (p["aid"].toString());
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
                                NoteEv n;
                                n.start = a + st + ((st % 2) ? swing : 0.0);
                                n.end = n.start + 0.9;
                                n.pitch = GM_DRUMS[l];
                                n.vel = val == 2 ? 1.0f : 0.62f;
                                td.notes.push_back (n);
                            }
                        }
                    }
                    else if (const auto* notes = p["notes"].getArray())
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
            sd->tracks.push_back (std::move (td));
        }
    }
    return sd;
}
