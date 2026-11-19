#include "Params.h"
#include "Lang.h"

namespace nova
{
    namespace
    {
        juce::StringArray split (const char* utf8) { return juce::StringArray::fromTokens (juce::String::fromUTF8 (utf8), "|", ""); }

        std::vector<ParamSpec> build()
        {
            std::vector<ParamSpec> v;
            auto F = [&v] (juce::String id, const char* name, float lo, float hi, float def, juce::String unit, float skew = 1.0f)
            { ParamSpec s; s.id = id; s.name = juce::String::fromUTF8 (name); s.kind = 0; s.lo = lo; s.hi = hi; s.def = def; s.unit = unit; s.skew = skew; v.push_back (s); };
            auto C = [&v] (juce::String id, const char* name, const char* choices, int def)
            { ParamSpec s; s.id = id; s.name = juce::String::fromUTF8 (name); s.kind = 1; s.choices = split (choices); s.lo = 0; s.hi = (float) s.choices.size() - 1; s.def = (float) def; v.push_back (s); };
            auto B = [&v] (juce::String id, const char* name, bool def)
            { ParamSpec s; s.id = id; s.name = juce::String::fromUTF8 (name); s.kind = 2; s.lo = 0; s.hi = 1; s.def = def ? 1.0f : 0.0f; v.push_back (s); };

            // makra jednoduchej verzie (ID z prvej verzie - kvoli ulozenym projektom)
            {
                juce::StringArray names;
                for (auto& f : factoryPatches()) names.add (f.name);
                ParamSpec s; s.id = "preset"; s.name = juce::String::fromUTF8 ("Zvuk"); s.kind = 1; s.choices = names; s.lo = 0; s.hi = (float) names.size() - 1; s.def = 0; v.push_back (s);
            }
            F ("bright", "Jas", 0, 1, 0.5f, "%");
            F ("attack", "Nábeh", 0, 1, 0.5f, "%");
            F ("release", "Dozvuk", 0, 1, 0.5f, "%");
            F ("space", "Priestor", 0, 1, 0.5f, "%");
            F ("width", "Šírka", 0, 1, 0.5f, "%");
            F ("volume", "Hlasitosť", -24, 6, 0, "dB");

            for (auto L : { juce::String ("a"), juce::String ("b") })
            {
                const juce::String p = L + "_", up = L.toUpperCase();
                B (p + "on", L == "a" ? "Vrstva A" : "Vrstva B", L == "a");
                C (p + "wave", "Priebeh", "Sínus|Trojuholník|Píla|Obdĺžnik|Šum|Pluck|FM|Sampel|Roj|Wavetable", 2);
                F (p + "oct", "Oktáva", -3, 3, 0, "oct");
                F (p + "semi", "Poltón", -12, 12, 0, "st");
                F (p + "fine", "Jemne", -100, 100, 0, "ct");
                F (p + "uni", "Unison", 1, 8, 1, "x");
                F (p + "spread", "Rozladenie", 0, 50, 10, "ct");
                F (p + "level", "Úroveň", 0, 1, 0.7f, "%");
                F (p + "pan", "Panoráma", -1, 1, 0, "pan");
                F (p + "color", "Farba", 0, 1, 0.5f, "%");
                C (p + "ratio", "FM pomer", "0.5|1|1.5|2|3|3.5|4|5|7|14", 1);
                C (p + "ftype", "Filter", "Dolná|Pásmová|Horná|Telo", 0);
                B (p + "f24", "24 dB", false);
                F (p + "cut", "Cutoff", 20, 20000, 20000, "Hz", 0.25f);
                F (p + "reso", "Rezonancia", 0, 1, 0.1f, "%");
                F (p + "fenv", "Obálka", -1, 1, 0, "bip");
                F (p + "fa", "Nábeh", 0.0005f, 10, 0.005f, "s", 0.3f);
                F (p + "fd", "Pokles", 0.001f, 20, 0.3f, "s", 0.3f);
                F (p + "fs", "Držanie", 0, 1, 1, "%");
                F (p + "fr", "Doznenie", 0.001f, 20, 0.3f, "s", 0.3f);
                F (p + "aa", "Nábeh", 0.0005f, 10, 0.003f, "s", 0.3f);
                F (p + "ad", "Pokles", 0.001f, 20, 0.3f, "s", 0.3f);
                F (p + "as", "Držanie", 0, 1, 1, "%");
                F (p + "ar", "Doznenie", 0.001f, 20, 0.3f, "s", 0.3f);
                C (p + "lshape", "LFO tvar", "Sínus|Troj.|Obdĺž.|Náhod.", 0);
                C (p + "ltgt", "LFO cieľ", "Výška|Filter|Hlasit.|Panor.|Farba", 0);
                F (p + "lrate", "Rýchlosť", 0.05f, 20, 5, "Hz", 0.4f);
                F (p + "ldepth", "Hĺbka", 0, 1, 0, "%");
                F (p + "ldelay", "Oneskorenie", 0, 3, 0, "s", 0.5f);
                C (p + "src", "Zdroj", "Klavír|Vlastný WAV", 0);
                F (p + "root", "Základný tón", 0, 127, 60, "note");
                B (p + "loop", "Opakovať", false);
                C (p + "wt", "Tabuľka", "Analóg|PWM|Harmonický|Vokál|Digitál|Organ|Sklo|Spev", 0);
                F (p + "penv", "Skok výšky", -24, 24, 0, "st");
                F (p + "ptime", "Čas skoku", 0.002f, 2, 0.08f, "s", 0.3f);
                F (p + "ktrk", "Podľa klávesu", 0, 1, 0.5f, "%");
                F (p + "fvel", "Podľa úderu", 0, 1, 0.5f, "%");
                F (p + "noise", "Šum", 0, 1, 0, "%");
            }
            // tube
            F ("tube", "Analóg", 0, 1, 0, "%");
            B ("comp_on", "Tube komp", false);
            F ("comp", "Kompresia", 0, 1, 0.35f, "%");
            // efekty
            B ("drive_on", "Drive", false);
            F ("drive", "Drive", 0, 1, 0.3f, "%");
            B ("ch_on", "Modulácia", false);
            C ("ch_type", "Typ modulácie", "Chorus|Ensemble|Flanger|Phaser|Tremolo", 0);
            F ("ch_fb", "Spätná väzba", 0, 0.95f, 0.3f, "%");
            F ("ch_rate", "Rýchlosť", 0.05f, 5, 0.6f, "Hz", 0.5f);
            F ("ch_depth", "Hĺbka", 0, 1, 0.5f, "%");
            F ("ch_mix", "Mix", 0, 1, 0.4f, "%");
            B ("dl_on", "Delay", false);
            C ("dl_sync", "Synchro", "Voľne|1/4|1/8 bodka|1/8|1/8 triola|1/16", 0);
            F ("dl_time", "Čas", 10, 2000, 375, "ms", 0.5f);
            F ("dl_fb", "Opakovanie", 0, 0.95f, 0.35f, "%");
            F ("dl_tone", "Farba", 1000, 20000, 8000, "Hz", 0.4f);
            F ("dl_mix", "Mix", 0, 1, 0.25f, "%");
            B ("dl_pp", "Ping-pong", true);
            B ("rv_on", "Dozvuk", true);
            F ("rv_size", "Veľkosť", 0, 1, 0.5f, "%");
            F ("rv_pre", "Predelay", 0, 250, 0, "ms", 0.5f);
            F ("rv_damp", "Tlmenie", 0, 1, 0.4f, "%");
            F ("rv_mix", "Mix", 0, 1, 0.2f, "%");
            F ("fx_width", "Šírka", 0, 2, 1, "%");
            F ("fx_punch", "Úder", 0, 1, 0.5f, "%");
            F ("eq_low", "Basy", -15, 15, 0, "dB");
            F ("eq_mid", "Stredy", -15, 15, 0, "dB");
            F ("eq_midf", "Stredy Hz", 200, 8000, 1000, "Hz", 0.35f);
            F ("eq_high", "Výšky", -15, 15, 0, "dB");
            B ("eq_on", "EQ zapnutý", true);
            // EQ vrstiev a dozvuku
            for (auto pre : { "a_", "b_", "rv_", "dl_", "vc_" })
            {
                const juce::String px (pre);
                F (px + "eql", "EQ basy", -15, 15, 0, "dB");
                F (px + "eqm", "EQ stredy", -15, 15, 0, "dB");
                F (px + "eqmf", "EQ stredy Hz", 200, 8000, 1000, "Hz", 0.35f);
                F (px + "eqh", "EQ výšky", -15, 15, 0, "dB");
                B (px + "eqon", "EQ zapnutý", true);
            }
            // arpeggiator, hlas
            B ("arp_on", "Arpeggiátor", false);
            B ("arp_hold", "Držať", false);
            C ("arp_pat", "Vzor", "Hore|Dole|Hore/Dole|Ako hrám|Náhodne|Akord", 0);
            C ("arp_speed", "Rýchlosť", "1/4|1/8|1/8T|1/16|1/32", 3);
            F ("arp_oct", "Oktávy", 1, 4, 1, "x");
            F ("arp_len", "Dĺžka", 0.1f, 1, 0.6f, "%");
            B ("mono", "Mono", false);
            F ("glide", "Glide", 0, 2, 0, "s", 0.4f);
            F ("veltrack", "Citlivosť", 0, 1, 0.7f, "%");
            // morph
            F ("morph_x", "Morph A–B", -1, 1, 0, "bip");
            F ("morph_y", "Tmavý–Jasný", -1, 1, 0, "bip");
            // master
            B ("mf_on", "Master filter", false);
            C ("mf_type", "Typ", "Dolná|Horná", 0);
            F ("mf_cut", "Cutoff", 20, 20000, 20000, "Hz", 0.25f);
            F ("mf_reso", "Rezonancia", 0, 1, 0, "%");
            F ("mf_drive", "Drive", 0, 1, 0, "%");
            F ("p_gain", "Úroveň zvuku", -30, 6, 0, "dB");
            // rytmicky gater (sekanie zvuku do rytmu, synchronne s tempom)
            B ("gt_on", "Gater", false);
            C ("gt_rate", "Gater rýchlosť", "1/4|1/8|1/8T|1/16|1/32", 3);
            F ("gt_depth", "Gater hĺbka", 0, 1, 0.85f, "%");
            F ("gt_smooth", "Gater hladkosť", 0, 1, 0.25f, "%");
            F ("gt_swing", "Gater swing", 0, 0.5f, 0, "%");
            {
                static const bool defPat[16] = { 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1 };
                for (int k = 0; k < 16; ++k) B ("gt_s" + juce::String (k), ("Gater krok " + std::to_string (k + 1)).c_str(), defPat[k]);
            }
            // vocoder: Inspiria je nosic, spev z inej stopy (sidechain) je modulator
            B ("vc_on", "Vocoder", false);
            F ("vc_bands", "Vocoder pásma", 12, 40, 20, "");
            F ("vc_formant", "Vocoder formant", -12, 12, 0, "st");
            F ("vc_clarity", "Vocoder zrozumiteľnosť", 0, 1, 0.45f, "%");
            F ("vc_release", "Vocoder dozvuk", 10, 300, 80, "ms", 0.5f);
            F ("vc_dry", "Vocoder hlas", 0, 1, 0.15f, "%");
            F ("vc_mix", "Vocoder mix", 0, 1, 1.0f, "%");   // 0 = len syntezator, 1 = len vocoder
            F ("vc_src", "Vocoder stopa", 0, 32, 0, "");   // vyber spevovej stopy v Korgus Sequenceri (0 = ziadna)
            // modulacna matica: 4 sloty zdroj -> ciel x mnozstvo
            for (int k = 1; k <= 4; ++k)
            {
                const juce::String m = "mm" + juce::String (k) + "_";
                C (m + "src", "Mod zdroj", "Vypnuté|LFO A|LFO B|Obálka filtra|Obálka hlasitosti|Sila úderu|Mod koliesko|Rytmus (gater)|Náhoda", 0);
                C (m + "dst", "Mod cieľ", "Cutoff A|Cutoff B|Cutoff A+B|Úroveň A|Úroveň B|Úroveň A+B|Výška A|Výška B|Výška A+B|Farba A|Farba B|Farba A+B|Panoráma A+B|Rezonancia A+B", 2);
                F (m + "amt", "Mod množstvo", -1, 1, 0, "bip");
            }
            // spustace pre hostitela (Korgus Sequencer): kazda zmena = novy nahodny zvuk / obmena
            B ("x_rnd", "Surprise / Prekvap ma", false);
            B ("x_mut", "Vary / Obmena", false);
            return v;
        }

        // pomocnik na zapis tovarenskych zvukov
        struct P
        {
            FactoryPatch f;
            P (const char* n, const char* c) { f.name = juce::String::fromUTF8 (n); f.category = juce::String::fromUTF8 (c); }
            P& s (const char* id, float v) { f.values[id] = v; return *this; }
        };
    }

    const juce::StringArray& categories()
    {
        static const juce::StringArray c = split ("Klavír|Klávesy|Pady|Sláčiky|Plucky|Zvonky|Basy|Leady|Moje zvuky");
        return c;
    }

    const std::vector<FactoryPatch>& factoryPatches()
    {
        static const std::vector<FactoryPatch> list = []
        {
            std::vector<FactoryPatch> v;
            // 0 koncertne piano
            v.push_back (P ("Koncertné piano", "Klavír").s ("a_wave", 7).s ("a_src", 0).s ("a_color", 0.5f).s ("a_level", 1).s ("a_aa", 0.0015f).s ("a_ar", 0.4f)
                         .s ("eq_high", 0.8f).s ("rv_mix", 0.16f).s ("rv_size", 0.45f).s ("rv_damp", 0.45f).s ("p_gain", -0.5f).s ("veltrack", 0).f);
            // 1 makke piano
            v.push_back (P ("Mäkké piano", "Klavír").s ("a_wave", 7).s ("a_src", 0).s ("a_color", 0.1f).s ("a_level", 1).s ("a_aa", 0.0015f).s ("a_ar", 0.4f)
                         .s ("eq_high", -7).s ("rv_mix", 0.3f).s ("rv_size", 0.62f).s ("rv_damp", 0.55f).s ("p_gain", 0).s ("veltrack", 0).f);
            // 2 elektricke piano: FM 1:1 + kratky "tine" (sinus ~14x) + autopan
            v.push_back (P ("Elektrické piano", "Klávesy").s ("a_wave", 6).s ("a_ratio", 1).s ("a_color", 0.4f).s ("a_fenv", 0.75f).s ("a_fa", 0.001f).s ("a_fd", 1.17f).s ("a_fs", 0)
                         .s ("a_aa", 0.002f).s ("a_ad", 6).s ("a_as", 0).s ("a_ar", 0.35f).s ("a_level", 1).s ("a_ltgt", 3).s ("a_lrate", 4.8f).s ("a_ldepth", 0.22f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", 3).s ("b_semi", 10).s ("b_level", 0.12f).s ("b_aa", 0.001f).s ("b_ad", 0.12f).s ("b_as", 0).s ("b_ar", 0.05f)
                         .s ("ch_on", 1).s ("ch_mix", 0.35f).s ("ch_depth", 0.6f).s ("ch_rate", 0.4f).s ("rv_mix", 0.18f).s ("rv_size", 0.5f).s ("p_gain", -8.2f).s ("veltrack", 0.9f).f);
            // 3 teply pad
            v.push_back (P ("Teplý pad", "Pady").s ("a_wave", 2).s ("a_uni", 5).s ("a_spread", 14).s ("a_level", 0.8f).s ("a_cut", 900).s ("a_reso", 0.18f).s ("a_fenv", 0.24f)
                         .s ("a_fa", 1.2f).s ("a_fd", 2.5f).s ("a_fs", 0.4f).s ("a_fr", 1.5f).s ("a_aa", 0.9f).s ("a_ad", 1.5f).s ("a_as", 0.85f).s ("a_ar", 1.6f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", -1).s ("b_level", 0.3f).s ("b_aa", 0.9f).s ("b_ad", 1.5f).s ("b_as", 0.85f).s ("b_ar", 1.6f)
                         .s ("tube", 0.5f).s ("ch_on", 1).s ("ch_mix", 0.45f).s ("ch_depth", 0.5f).s ("rv_mix", 0.4f).s ("rv_size", 0.75f).s ("rv_damp", 0.45f).s ("p_gain", -14.4f).s ("veltrack", 0.7f).f);
            // 4 skleneny pad
            v.push_back (P ("Sklenený pad", "Pady").s ("a_wave", 3).s ("a_color", 0.3f).s ("a_uni", 4).s ("a_spread", 9).s ("a_level", 0.8f).s ("a_cut", 2600).s ("a_reso", 0.32f).s ("a_fenv", 0.16f)
                         .s ("a_fa", 1.5f).s ("a_fd", 3).s ("a_fs", 0.6f).s ("a_fr", 2).s ("a_aa", 1.2f).s ("a_ad", 2).s ("a_as", 0.9f).s ("a_ar", 2.2f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", 2).s ("b_level", 0.3f).s ("b_aa", 1.2f).s ("b_ad", 2).s ("b_as", 0.9f).s ("b_ar", 2.2f)
                         .s ("tube", 0.6f).s ("ch_on", 1).s ("ch_mix", 0.5f).s ("rv_mix", 0.5f).s ("rv_size", 0.85f).s ("rv_damp", 0.25f).s ("p_gain", -28.3f).s ("veltrack", 0.7f).f);
            // 5 slaciky
            v.push_back (P ("Sláčiky", "Sláčiky").s ("a_wave", 2).s ("a_uni", 6).s ("a_spread", 11).s ("a_level", 0.85f).s ("a_cut", 2400).s ("a_reso", 0.17f).s ("a_fenv", 0.14f)
                         .s ("a_fa", 0.35f).s ("a_fd", 1).s ("a_fs", 0.7f).s ("a_fr", 0.6f).s ("a_aa", 0.32f).s ("a_ad", 0.6f).s ("a_as", 0.9f).s ("a_ar", 0.7f)
                         .s ("a_lrate", 5.2f).s ("a_ldepth", 0.09f).s ("a_ldelay", 0.35f).s ("tube", 0.5f)
                         .s ("ch_on", 1).s ("ch_mix", 0.55f).s ("ch_depth", 0.65f).s ("rv_mix", 0.35f).s ("rv_size", 0.7f).s ("p_gain", -14.4f).s ("veltrack", 0.7f).f);
            // 6 husle: pila cez filter "Telo" (rezonancie tela), vibrato, sum slaku
            v.push_back (P ("Husle", "Sláčiky").s ("a_wave", 2).s ("a_uni", 2).s ("a_spread", 4).s ("a_level", 0.9f).s ("a_ftype", 3).s ("a_cut", 4200).s ("a_reso", 0.1f).s ("a_fenv", 0.1f)
                         .s ("a_fa", 0.12f).s ("a_fd", 0.6f).s ("a_fs", 0.75f).s ("a_fr", 0.4f).s ("a_aa", 0.12f).s ("a_ad", 0.4f).s ("a_as", 0.9f).s ("a_ar", 0.35f)
                         .s ("a_lrate", 5.6f).s ("a_ldepth", 0.22f).s ("a_ldelay", 0.28f)
                         .s ("b_on", 1).s ("b_wave", 4).s ("b_level", 0.06f).s ("b_ftype", 1).s ("b_cut", 2500).s ("b_reso", 0.2f).s ("b_aa", 0.005f).s ("b_ad", 0.45f).s ("b_as", 0.15f).s ("b_ar", 0.3f)
                         .s ("ch_on", 1).s ("ch_mix", 0.15f).s ("rv_mix", 0.32f).s ("rv_size", 0.6f).s ("p_gain", -18.4f).s ("veltrack", 0.7f).f);
            // 7 pluck (Karplus-Strong): pri Pluck je "Pokles" dlzka doznievania struny
            v.push_back (P ("Pluck", "Plucky").s ("a_wave", 5).s ("a_color", 0.75f).s ("a_level", 1).s ("a_aa", 0.001f).s ("a_ad", 1.6f).s ("a_as", 1).s ("a_ar", 0.2f)
                         .s ("ch_on", 1).s ("ch_mix", 0.2f).s ("rv_mix", 0.25f).s ("rv_size", 0.55f).s ("p_gain", -4.4f).s ("veltrack", 0.85f).f);
            // 8 harfa
            v.push_back (P ("Harfa", "Plucky").s ("a_wave", 5).s ("a_color", 0.45f).s ("a_level", 1).s ("a_aa", 0.001f).s ("a_ad", 5).s ("a_as", 1).s ("a_ar", 1.2f)
                         .s ("b_on", 1).s ("b_wave", 5).s ("b_fine", 3.5f).s ("b_color", 0.45f).s ("b_level", 0.4f).s ("b_aa", 0.001f).s ("b_ad", 5).s ("b_as", 1).s ("b_ar", 1.2f)
                         .s ("ch_on", 1).s ("ch_mix", 0.1f).s ("rv_mix", 0.38f).s ("rv_size", 0.75f).s ("p_gain", -6.6f).s ("veltrack", 0.85f).f);
            // 9 celesta
            v.push_back (P ("Celesta", "Zvonky").s ("a_wave", 6).s ("a_ratio", 5).s ("a_color", 0.225f).s ("a_fenv", 0.95f).s ("a_fa", 0.001f).s ("a_fd", 0.5f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 5).s ("a_as", 0).s ("a_ar", 0.6f).s ("a_level", 1)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", 2).s ("b_semi", 10).s ("b_level", 0.08f).s ("b_aa", 0.001f).s ("b_ad", 0.12f).s ("b_as", 0).s ("b_ar", 0.05f)
                         .s ("ch_on", 1).s ("ch_mix", 0.15f).s ("rv_mix", 0.35f).s ("rv_size", 0.6f).s ("p_gain", -11.4f).s ("veltrack", 0.9f).f);
            // 10 teply bas
            v.push_back (P ("Teplý bas", "Basy").s ("a_wave", 2).s ("a_level", 0.8f).s ("a_cut", 600).s ("a_reso", 0.2f).s ("a_fenv", 0.35f).s ("a_fa", 0.002f).s ("a_fd", 0.35f).s ("a_fs", 0.2f).s ("a_fr", 0.12f)
                         .s ("a_aa", 0.003f).s ("a_ad", 0.5f).s ("a_as", 0.8f).s ("a_ar", 0.12f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", -1).s ("b_level", 0.6f).s ("b_aa", 0.003f).s ("b_ad", 0.5f).s ("b_as", 0.8f).s ("b_ar", 0.12f)
                         .s ("mono", 1).s ("glide", 0.03f).s ("tube", 0.4f).s ("rv_on", 0).s ("p_gain", -10.4f).s ("veltrack", 0.5f).f);
            // 11 sub bas
            v.push_back (P ("Sub bas", "Basy").s ("a_wave", 0).s ("a_level", 0.9f).s ("a_aa", 0.004f).s ("a_ad", 0.3f).s ("a_as", 1).s ("a_ar", 0.1f)
                         .s ("b_on", 1).s ("b_wave", 1).s ("b_level", 0.2f).s ("b_aa", 0.004f).s ("b_ad", 0.3f).s ("b_as", 1).s ("b_ar", 0.1f)
                         .s ("mono", 1).s ("rv_on", 0).s ("p_gain", -9.5f).s ("veltrack", 0.4f).f);
            // 12 makky lead
            v.push_back (P ("Mäkký lead", "Leady").s ("a_wave", 2).s ("a_uni", 3).s ("a_spread", 8).s ("a_level", 0.8f).s ("a_cut", 2200).s ("a_reso", 0.25f).s ("a_fenv", 0.2f)
                         .s ("a_fa", 0.01f).s ("a_fd", 0.6f).s ("a_fs", 0.5f).s ("a_fr", 0.3f).s ("a_aa", 0.01f).s ("a_ad", 0.4f).s ("a_as", 0.9f).s ("a_ar", 0.25f)
                         .s ("a_lrate", 5.5f).s ("a_ldepth", 0.12f).s ("a_ldelay", 0.3f).s ("mono", 1).s ("glide", 0.06f)
                         .s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.18f).s ("dl_fb", 0.3f).s ("rv_mix", 0.2f).s ("p_gain", -9).s ("veltrack", 0.6f).f);
            // 13 organ
            v.push_back (P ("Organ", "Klávesy").s ("a_wave", 0).s ("a_level", 0.6f).s ("a_aa", 0.005f).s ("a_ad", 0.1f).s ("a_as", 1).s ("a_ar", 0.08f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", 1).s ("b_level", 0.4f).s ("b_aa", 0.005f).s ("b_ad", 0.1f).s ("b_as", 1).s ("b_ar", 0.08f)
                         .s ("drive_on", 1).s ("drive", 0.15f).s ("ch_on", 1).s ("ch_mix", 0.5f).s ("ch_rate", 0.8f).s ("rv_mix", 0.18f).s ("p_gain", -22).s ("veltrack", 0).f);

            // ---------------------------------------------------------------- dalsie tovarenske zvuky
            // klavir
            v.push_back (P ("Jasný klavír", "Klavír").s ("a_wave", 7).s ("a_src", 0).s ("a_color", 0.8f).s ("a_level", 1).s ("a_aa", 0.0015f).s ("a_ar", 0.35f)
                         .s ("eq_high", 2.5f).s ("comp_on", 1).s ("comp", 0.25f).s ("rv_mix", 0.12f).s ("rv_size", 0.4f).s ("p_gain", -2.3f).s ("veltrack", 0).f);
            v.push_back (P ("Klavír v hale", "Klavír").s ("a_wave", 7).s ("a_src", 0).s ("a_color", 0.5f).s ("a_level", 1).s ("a_aa", 0.0015f).s ("a_ar", 0.5f)
                         .s ("rv_mix", 0.34f).s ("rv_size", 0.82f).s ("rv_damp", 0.5f).s ("rv_pre", 30).s ("p_gain", -2.8f).s ("veltrack", 0).f);
            v.push_back (P ("Klavír a pad", "Klavír").s ("a_wave", 7).s ("a_src", 0).s ("a_color", 0.5f).s ("a_level", 1).s ("a_aa", 0.0015f).s ("a_ar", 0.4f)
                         .s ("b_on", 1).s ("b_wave", 2).s ("b_uni", 4).s ("b_spread", 12).s ("b_level", 0.2f).s ("b_cut", 1800).s ("b_aa", 1.2f).s ("b_ad", 1.5f).s ("b_as", 0.9f).s ("b_ar", 1.8f)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.3f).s ("rv_mix", 0.3f).s ("rv_size", 0.75f).s ("p_gain", -1.1f).s ("veltrack", 0.2f).f);
            v.push_back (P ("Lo-fi klavír", "Klavír").s ("a_wave", 7).s ("a_src", 0).s ("a_color", 0.3f).s ("a_level", 1).s ("a_aa", 0.0015f).s ("a_ar", 0.3f)
                         .s ("tube", 0.5f).s ("eq_low", 2).s ("eq_high", -6).s ("mf_on", 1).s ("mf_cut", 3800).s ("ch_on", 1).s ("ch_rate", 0.35f).s ("ch_depth", 0.35f).s ("ch_mix", 0.3f)
                         .s ("rv_mix", 0.22f).s ("rv_size", 0.5f).s ("p_gain", -3.4f).s ("veltrack", 0).f);
            // klavesy
            v.push_back (P ("Kalné EP", "Klávesy").s ("a_wave", 6).s ("a_ratio", 1).s ("a_color", 0.55f).s ("a_fenv", 0.6f).s ("a_fa", 0.001f).s ("a_fd", 0.8f).s ("a_fs", 0)
                         .s ("a_aa", 0.002f).s ("a_ad", 4).s ("a_as", 0.1f).s ("a_ar", 0.3f).s ("a_level", 1).s ("tube", 0.3f)
                         .s ("ch_on", 1).s ("ch_type", 4).s ("ch_rate", 4.5f).s ("ch_depth", 0.6f).s ("ch_mix", 0.3f).s ("rv_mix", 0.18f).s ("p_gain", -9.7f).s ("veltrack", 0.85f).f);
            v.push_back (P ("Zvonivé EP", "Klávesy").s ("a_wave", 6).s ("a_ratio", 9).s ("a_color", 0.12f).s ("a_fenv", 0.9f).s ("a_fa", 0.001f).s ("a_fd", 0.4f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 3.5f).s ("a_as", 0).s ("a_ar", 0.5f).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_level", 0.6f).s ("b_aa", 0.002f).s ("b_ad", 3).s ("b_as", 0.1f).s ("b_ar", 0.4f)
                         .s ("ch_on", 1).s ("ch_mix", 0.35f).s ("rv_mix", 0.22f).s ("p_gain", -12.8f).s ("veltrack", 0.85f).f);
            v.push_back (P ("Synthwave klávesy", "Klávesy").s ("a_wave", 2).s ("a_uni", 3).s ("a_spread", 10).s ("a_cut", 2400).s ("a_reso", 0.2f).s ("a_fenv", 0.4f)
                         .s ("a_fa", 0.002f).s ("a_fd", 0.6f).s ("a_fs", 0.3f).s ("a_aa", 0.003f).s ("a_ad", 1.2f).s ("a_as", 0.4f).s ("a_ar", 0.5f).s ("a_level", 0.8f)
                         .s ("ch_on", 1).s ("ch_mix", 0.45f).s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.2f).s ("dl_fb", 0.3f).s ("rv_mix", 0.25f).s ("p_gain", -7.3f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Funky klávesy", "Klávesy").s ("a_wave", 3).s ("a_color", 0.2f).s ("a_cut", 2800).s ("a_reso", 0.3f).s ("a_fenv", 0.5f).s ("a_fa", 0.001f).s ("a_fd", 0.15f).s ("a_fs", 0.1f)
                         .s ("a_aa", 0.001f).s ("a_ad", 0.3f).s ("a_as", 0).s ("a_ar", 0.08f).s ("a_level", 0.8f).s ("rv_mix", 0.1f).s ("p_gain", -7.4f).s ("veltrack", 0.9f).f);
            v.push_back (P ("Rockový organ", "Klávesy").s ("a_wave", 0).s ("a_level", 0.6f).s ("a_aa", 0.004f).s ("a_ad", 0.1f).s ("a_as", 1).s ("a_ar", 0.06f)
                         .s ("b_on", 1).s ("b_wave", 1).s ("b_oct", 1).s ("b_level", 0.45f).s ("b_aa", 0.004f).s ("b_ad", 0.1f).s ("b_as", 1).s ("b_ar", 0.06f)
                         .s ("drive_on", 1).s ("drive", 0.4f).s ("ch_on", 1).s ("ch_rate", 5.5f).s ("ch_depth", 0.4f).s ("ch_mix", 0.5f).s ("rv_mix", 0.15f).s ("p_gain", -14.5f).s ("veltrack", 0).f);
            v.push_back (P ("Kostolný organ", "Klávesy").s ("a_wave", 1).s ("a_level", 0.6f).s ("a_aa", 0.05f).s ("a_ad", 0.1f).s ("a_as", 1).s ("a_ar", 0.4f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", 1).s ("b_level", 0.4f).s ("b_aa", 0.05f).s ("b_ad", 0.1f).s ("b_as", 1).s ("b_ar", 0.4f)
                         .s ("rv_mix", 0.5f).s ("rv_size", 0.95f).s ("rv_damp", 0.35f).s ("rv_pre", 40).s ("p_gain", -13.9f).s ("veltrack", 0).f);
            // pady
            v.push_back (P ("Ambientný oblak", "Pady").s ("a_wave", 2).s ("a_uni", 7).s ("a_spread", 22).s ("a_cut", 1200).s ("a_reso", 0.15f).s ("a_fenv", 0.2f)
                         .s ("a_fa", 3).s ("a_fd", 3).s ("a_fs", 0.6f).s ("a_fr", 3).s ("a_aa", 2.5f).s ("a_ad", 2).s ("a_as", 0.9f).s ("a_ar", 4).s ("a_level", 0.75f)
                         .s ("a_ltgt", 1).s ("a_lrate", 0.1f).s ("a_ldepth", 0.3f)
                         .s ("b_on", 1).s ("b_wave", 1).s ("b_oct", 1).s ("b_level", 0.2f).s ("b_aa", 2.5f).s ("b_ad", 2).s ("b_as", 0.9f).s ("b_ar", 4)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.5f).s ("rv_mix", 0.55f).s ("rv_size", 0.95f).s ("rv_pre", 40).s ("p_gain", -6.0f).s ("veltrack", 0.5f).f);
            v.push_back (P ("Temný pad", "Pady").s ("a_wave", 3).s ("a_color", 0.3f).s ("a_uni", 5).s ("a_spread", 12).s ("a_cut", 520).s ("a_reso", 0.25f).s ("a_fenv", 0.1f)
                         .s ("a_aa", 1.4f).s ("a_ad", 2).s ("a_as", 0.9f).s ("a_ar", 2.5f).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", -1).s ("b_level", 0.35f).s ("b_aa", 1.4f).s ("b_ad", 2).s ("b_as", 0.9f).s ("b_ar", 2.5f)
                         .s ("eq_high", -4).s ("rv_mix", 0.45f).s ("rv_size", 0.85f).s ("p_gain", -18.6f).s ("veltrack", 0.5f).f);
            v.push_back (P ("Zimný pad", "Pady").s ("a_wave", 1).s ("a_uni", 4).s ("a_spread", 14).s ("a_cut", 4000).s ("a_aa", 1.5f).s ("a_ad", 2).s ("a_as", 0.9f).s ("a_ar", 2.5f).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 4).s ("b_level", 0.05f).s ("b_ftype", 1).s ("b_cut", 3000).s ("b_reso", 0.3f).s ("b_aa", 1.5f).s ("b_ad", 2).s ("b_as", 0.9f).s ("b_ar", 2.5f)
                         .s ("ch_on", 1).s ("ch_type", 3).s ("ch_rate", 0.2f).s ("ch_depth", 0.6f).s ("ch_fb", 0.4f).s ("ch_mix", 0.45f).s ("rv_mix", 0.5f).s ("rv_size", 0.85f).s ("p_gain", -8.6f).s ("veltrack", 0.5f).f);
            v.push_back (P ("Synthwave pad", "Pady").s ("a_wave", 2).s ("a_uni", 6).s ("a_spread", 16).s ("a_cut", 2200).s ("a_reso", 0.2f).s ("a_fenv", 0.15f).s ("a_fa", 0.8f).s ("a_fd", 2)
                         .s ("a_fs", 0.5f).s ("a_aa", 0.6f).s ("a_ad", 1.5f).s ("a_as", 0.85f).s ("a_ar", 1.8f).s ("a_level", 0.8f)
                         .s ("ch_on", 1).s ("ch_mix", 0.5f).s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.15f).s ("rv_mix", 0.4f).s ("rv_size", 0.8f).s ("p_gain", -14.0f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Sklenené zvony", "Pady").s ("a_wave", 6).s ("a_ratio", 5).s ("a_color", 0.25f).s ("a_fenv", 0.4f).s ("a_fa", 0.6f).s ("a_fd", 2).s ("a_fs", 0.3f)
                         .s ("a_aa", 0.5f).s ("a_ad", 2).s ("a_as", 0.8f).s ("a_ar", 3).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 2).s ("b_uni", 4).s ("b_spread", 14).s ("b_cut", 1600).s ("b_level", 0.3f).s ("b_aa", 1.2f).s ("b_ad", 2).s ("b_as", 0.9f).s ("b_ar", 3)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.4f).s ("rv_mix", 0.5f).s ("rv_size", 0.9f).s ("rv_pre", 25).s ("p_gain", -14.7f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Dych", "Pady").s ("a_wave", 4).s ("a_color", 0.6f).s ("a_ftype", 1).s ("a_cut", 1200).s ("a_reso", 0.5f).s ("a_aa", 1.5f).s ("a_ad", 2).s ("a_as", 0.9f).s ("a_ar", 2).s ("a_level", 0.5f)
                         .s ("a_ltgt", 1).s ("a_lrate", 0.15f).s ("a_ldepth", 0.35f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_level", 0.5f).s ("b_aa", 1.5f).s ("b_ad", 2).s ("b_as", 0.9f).s ("b_ar", 2)
                         .s ("rv_mix", 0.5f).s ("rv_size", 0.85f).s ("p_gain", -13.1f).s ("veltrack", 0.5f).f);
            // slaciky
            v.push_back (P ("Pomalé sláčiky", "Sláčiky").s ("a_wave", 2).s ("a_uni", 6).s ("a_spread", 12).s ("a_cut", 2200).s ("a_reso", 0.15f).s ("a_fenv", 0.12f).s ("a_fa", 1).s ("a_fd", 1.5f).s ("a_fs", 0.7f)
                         .s ("a_aa", 1.2f).s ("a_ad", 1).s ("a_as", 0.9f).s ("a_ar", 1.5f).s ("a_level", 0.85f).s ("a_lrate", 5.2f).s ("a_ldepth", 0.07f).s ("a_ldelay", 0.6f)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.55f).s ("rv_mix", 0.42f).s ("rv_size", 0.85f).s ("rv_pre", 20).s ("p_gain", -13.5f).s ("veltrack", 0.7f).f);
            v.push_back (P ("Pizzicato", "Sláčiky").s ("a_wave", 5).s ("a_color", 0.4f).s ("a_level", 1).s ("a_aa", 0.001f).s ("a_ad", 0.5f).s ("a_as", 1).s ("a_ar", 0.15f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_level", 0.25f).s ("b_aa", 0.002f).s ("b_ad", 0.25f).s ("b_as", 0).s ("b_ar", 0.1f)
                         .s ("rv_mix", 0.3f).s ("rv_size", 0.7f).s ("p_gain", 0.3f).s ("veltrack", 0.8f).f);
            v.push_back (P ("Violončelo", "Sláčiky").s ("a_wave", 2).s ("a_oct", -1).s ("a_uni", 2).s ("a_spread", 4).s ("a_ftype", 3).s ("a_cut", 3200).s ("a_fenv", 0.1f).s ("a_fa", 0.15f).s ("a_fd", 0.6f).s ("a_fs", 0.75f)
                         .s ("a_aa", 0.15f).s ("a_ad", 0.4f).s ("a_as", 0.9f).s ("a_ar", 0.45f).s ("a_level", 0.9f).s ("a_lrate", 5.3f).s ("a_ldepth", 0.18f).s ("a_ldelay", 0.4f)
                         .s ("ch_on", 1).s ("ch_mix", 0.15f).s ("rv_mix", 0.32f).s ("rv_size", 0.65f).s ("p_gain", -16.0f).s ("veltrack", 0.7f).f);
            v.push_back (P ("Orchester", "Sláčiky").s ("a_wave", 2).s ("a_uni", 7).s ("a_spread", 14).s ("a_cut", 3000).s ("a_reso", 0.1f).s ("a_fenv", 0.1f).s ("a_fa", 0.4f).s ("a_fd", 1).s ("a_fs", 0.7f)
                         .s ("a_aa", 0.4f).s ("a_ad", 0.8f).s ("a_as", 0.9f).s ("a_ar", 1).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 3).s ("b_oct", -1).s ("b_color", 0.4f).s ("b_cut", 1500).s ("b_level", 0.4f).s ("b_aa", 0.4f).s ("b_ad", 0.8f).s ("b_as", 0.9f).s ("b_ar", 1)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.5f).s ("rv_mix", 0.45f).s ("rv_size", 0.85f).s ("rv_pre", 25).s ("p_gain", -15.0f).s ("veltrack", 0.7f).f);
            // plucky
            v.push_back (P ("Nylonová gitara", "Plucky").s ("a_wave", 5).s ("a_color", 0.35f).s ("a_level", 1).s ("a_aa", 0.001f).s ("a_ad", 2.5f).s ("a_as", 1).s ("a_ar", 0.5f)
                         .s ("b_on", 1).s ("b_wave", 5).s ("b_fine", 2).s ("b_color", 0.3f).s ("b_level", 0.3f).s ("b_aa", 0.001f).s ("b_ad", 2.5f).s ("b_as", 1).s ("b_ar", 0.5f)
                         .s ("rv_mix", 0.2f).s ("rv_size", 0.55f).s ("p_gain", -3.8f).s ("veltrack", 0.85f).f);
            v.push_back (P ("Kalimba", "Plucky").s ("a_wave", 6).s ("a_ratio", 4).s ("a_color", 0.3f).s ("a_fenv", 1).s ("a_fa", 0.001f).s ("a_fd", 0.08f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 1.2f).s ("a_as", 0).s ("a_ar", 0.3f).s ("a_level", 1)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", 2).s ("b_level", 0.12f).s ("b_aa", 0.001f).s ("b_ad", 0.15f).s ("b_as", 0).s ("b_ar", 0.1f)
                         .s ("rv_mix", 0.25f).s ("rv_size", 0.6f).s ("p_gain", -9.3f).s ("veltrack", 0.85f).f);
            v.push_back (P ("Synth pluck", "Plucky").s ("a_wave", 2).s ("a_uni", 2).s ("a_spread", 8).s ("a_cut", 400).s ("a_reso", 0.3f).s ("a_fenv", 0.8f).s ("a_fa", 0.001f).s ("a_fd", 0.18f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 0.5f).s ("a_as", 0).s ("a_ar", 0.3f).s ("a_level", 0.9f)
                         .s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.25f).s ("dl_fb", 0.35f).s ("rv_mix", 0.25f).s ("p_gain", -5.2f).s ("veltrack", 0.8f).f);
            v.push_back (P ("Marimba", "Plucky").s ("a_wave", 6).s ("a_ratio", 6).s ("a_color", 0.2f).s ("a_fenv", 1).s ("a_fa", 0.001f).s ("a_fd", 0.05f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 0.7f).s ("a_as", 0).s ("a_ar", 0.25f).s ("a_level", 1).s ("rv_mix", 0.22f).s ("rv_size", 0.55f).s ("p_gain", -7.3f).s ("veltrack", 0.85f).f);
            v.push_back (P ("Pluck v ozvene", "Plucky").s ("a_wave", 3).s ("a_color", 0.35f).s ("a_cut", 1500).s ("a_reso", 0.2f).s ("a_fenv", 0.5f).s ("a_fa", 0.001f).s ("a_fd", 0.12f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 0.25f).s ("a_as", 0).s ("a_ar", 0.15f).s ("a_level", 0.8f)
                         .s ("dl_on", 1).s ("dl_sync", 4).s ("dl_pp", 1).s ("dl_mix", 0.35f).s ("dl_fb", 0.5f).s ("dl_eql", -6).s ("rv_mix", 0.25f).s ("p_gain", -5.4f).s ("veltrack", 0.8f).f);
            // zvonky
            v.push_back (P ("Hudobná skrinka", "Zvonky").s ("a_wave", 6).s ("a_ratio", 8).s ("a_oct", 1).s ("a_color", 0.25f).s ("a_fenv", 0.9f).s ("a_fa", 0.001f).s ("a_fd", 0.3f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 2).s ("a_as", 0).s ("a_ar", 0.5f).s ("a_level", 1).s ("rv_mix", 0.3f).s ("rv_size", 0.65f).s ("p_gain", -10.5f).s ("veltrack", 0.8f).f);
            v.push_back (P ("Vibrafón", "Zvonky").s ("a_wave", 6).s ("a_ratio", 6).s ("a_color", 0.15f).s ("a_fenv", 0.6f).s ("a_fa", 0.001f).s ("a_fd", 0.3f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 3).s ("a_as", 0).s ("a_ar", 0.8f).s ("a_level", 1)
                         .s ("ch_on", 1).s ("ch_type", 4).s ("ch_rate", 5).s ("ch_depth", 0.3f).s ("ch_mix", 0.35f).s ("rv_mix", 0.28f).s ("p_gain", -10.5f).s ("veltrack", 0.85f).f);
            v.push_back (P ("Zvony", "Zvonky").s ("a_wave", 6).s ("a_ratio", 5).s ("a_color", 0.45f).s ("a_fenv", 0.8f).s ("a_fa", 0.001f).s ("a_fd", 1.5f).s ("a_fs", 0)
                         .s ("a_aa", 0.001f).s ("a_ad", 6).s ("a_as", 0).s ("a_ar", 3).s ("a_level", 1).s ("rv_mix", 0.38f).s ("rv_size", 0.85f).s ("p_gain", -11.9f).s ("veltrack", 0.85f).f);
            // basy
            v.push_back (P ("808 bas", "Basy").s ("a_wave", 0).s ("a_oct", -1).s ("a_level", 0.9f).s ("a_aa", 0.002f).s ("a_ad", 1.5f).s ("a_as", 0).s ("a_ar", 0.3f)
                         .s ("drive_on", 1).s ("drive", 0.3f).s ("tube", 0.2f).s ("mono", 1).s ("glide", 0.05f).s ("rv_on", 0).s ("p_gain", -15.4f).s ("veltrack", 0.4f).f);
            v.push_back (P ("Acid bas", "Basy").s ("a_wave", 2).s ("a_cut", 300).s ("a_reso", 0.75f).s ("a_fenv", 0.7f).s ("a_fa", 0.001f).s ("a_fd", 0.2f).s ("a_fs", 0).s ("a_fr", 0.1f)
                         .s ("a_aa", 0.002f).s ("a_ad", 0.4f).s ("a_as", 0.7f).s ("a_ar", 0.1f).s ("a_level", 0.8f)
                         .s ("drive_on", 1).s ("drive", 0.3f).s ("mono", 1).s ("glide", 0.06f).s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.12f).s ("rv_on", 0).s ("p_gain", -17.4f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Synthwave bas", "Basy").s ("a_wave", 2).s ("a_uni", 2).s ("a_spread", 6).s ("a_cut", 700).s ("a_reso", 0.2f).s ("a_fenv", 0.5f).s ("a_fa", 0.001f).s ("a_fd", 0.25f).s ("a_fs", 0.2f)
                         .s ("a_aa", 0.002f).s ("a_ad", 0.5f).s ("a_as", 0.7f).s ("a_ar", 0.12f).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 3).s ("b_oct", -1).s ("b_cut", 600).s ("b_level", 0.5f).s ("b_aa", 0.002f).s ("b_ad", 0.5f).s ("b_as", 0.7f).s ("b_ar", 0.12f)
                         .s ("rv_on", 0).s ("p_gain", -14.6f).s ("veltrack", 0.5f).f);
            v.push_back (P ("Kontrabas", "Basy").s ("a_wave", 5).s ("a_oct", -1).s ("a_color", 0.2f).s ("a_level", 1).s ("a_aa", 0.001f).s ("a_ad", 1.5f).s ("a_as", 1).s ("a_ar", 0.2f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", -1).s ("b_level", 0.4f).s ("b_aa", 0.005f).s ("b_ad", 0.6f).s ("b_as", 0).s ("b_ar", 0.15f)
                         .s ("rv_mix", 0.1f).s ("p_gain", -8.7f).s ("veltrack", 0.7f).f);
            v.push_back (P ("Reese bas", "Basy").s ("a_wave", 2).s ("a_oct", -1).s ("a_uni", 4).s ("a_spread", 25).s ("a_cut", 900).s ("a_reso", 0.2f).s ("a_aa", 0.003f).s ("a_ad", 0.5f).s ("a_as", 0.9f).s ("a_ar", 0.15f).s ("a_level", 0.8f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", -1).s ("b_level", 0.4f).s ("b_aa", 0.003f).s ("b_ad", 0.5f).s ("b_as", 0.9f).s ("b_ar", 0.15f)
                         .s ("mono", 1).s ("glide", 0.04f).s ("drive_on", 1).s ("drive", 0.2f).s ("rv_on", 0).s ("p_gain", -15.1f).s ("veltrack", 0.4f).f);
            // leady
            v.push_back (P ("Pílový lead", "Leady").s ("a_wave", 2).s ("a_uni", 4).s ("a_spread", 12).s ("a_cut", 5000).s ("a_reso", 0.2f).s ("a_fenv", 0.2f).s ("a_fa", 0.005f).s ("a_fd", 0.5f).s ("a_fs", 0.6f)
                         .s ("a_aa", 0.005f).s ("a_ad", 0.4f).s ("a_as", 0.9f).s ("a_ar", 0.2f).s ("a_level", 0.8f).s ("a_lrate", 5.5f).s ("a_ldepth", 0.1f).s ("a_ldelay", 0.35f)
                         .s ("mono", 1).s ("glide", 0.05f).s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.18f).s ("rv_mix", 0.22f).s ("p_gain", -8.5f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Píšťala", "Leady").s ("a_wave", 1).s ("a_level", 0.85f).s ("a_aa", 0.06f).s ("a_ad", 0.3f).s ("a_as", 0.95f).s ("a_ar", 0.2f).s ("a_lrate", 5).s ("a_ldepth", 0.08f).s ("a_ldelay", 0.3f)
                         .s ("b_on", 1).s ("b_wave", 4).s ("b_color", 0.7f).s ("b_ftype", 1).s ("b_cut", 2500).s ("b_reso", 0.3f).s ("b_level", 0.06f).s ("b_aa", 0.04f).s ("b_ad", 0.3f).s ("b_as", 0.8f).s ("b_ar", 0.2f)
                         .s ("rv_mix", 0.3f).s ("rv_size", 0.7f).s ("p_gain", -14.5f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Retro lead", "Leady").s ("a_wave", 3).s ("a_color", 0.5f).s ("a_level", 0.6f).s ("a_aa", 0.002f).s ("a_ad", 0.2f).s ("a_as", 0.9f).s ("a_ar", 0.1f)
                         .s ("a_lrate", 7).s ("a_ldepth", 0.06f).s ("a_ldelay", 0.25f).s ("mono", 1).s ("dl_on", 1).s ("dl_sync", 3).s ("dl_mix", 0.18f).s ("dl_fb", 0.25f)
                         .s ("rv_mix", 0.15f).s ("p_gain", -10.6f).s ("veltrack", 0.3f).f);
            v.push_back (P ("Spevavý lead", "Leady").s ("a_wave", 3).s ("a_color", 0.35f).s ("a_uni", 2).s ("a_spread", 6).s ("a_cut", 2500).s ("a_reso", 0.35f).s ("a_fenv", 0.15f)
                         .s ("a_aa", 0.04f).s ("a_ad", 0.4f).s ("a_as", 0.9f).s ("a_ar", 0.3f).s ("a_level", 0.8f).s ("a_lrate", 5.3f).s ("a_ldepth", 0.14f).s ("a_ldelay", 0.35f)
                         .s ("mono", 1).s ("glide", 0.1f).s ("rv_mix", 0.35f).s ("rv_size", 0.75f).s ("rv_pre", 50).s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.15f).s ("p_gain", -13.5f).s ("veltrack", 0.6f).f);
            // Roj (harmonicky roj, wave 8): Farba = Focus, Rozladenie = Rozptyl, Unison = Bohatost (4 alikvoty na krok); filtrova obalka riadi Focus
            v.push_back (P ("Roj Mellotron", "Pady").s ("a_wave", 8).s ("a_color", 0.7f).s ("a_uni", 5).s ("a_spread", 18).s ("a_level", 0.8f).s ("a_fenv", 0.83f)
                         .s ("a_fa", 1.6f).s ("a_fd", 0.5f).s ("a_fs", 1).s ("a_fr", 15).s ("a_aa", 1.0f).s ("a_ad", 1.5f).s ("a_as", 0.85f).s ("a_ar", 1.6f)
                         .s ("rv_mix", 0.45f).s ("rv_size", 0.85f).s ("rv_damp", 0.4f).s ("p_gain", -12.8f).s ("veltrack", 0.7f).f);
            v.push_back (P ("Roj zvony", "Zvonky").s ("a_wave", 8).s ("a_color", 0.3f).s ("a_uni", 4).s ("a_spread", 34).s ("a_level", 1).s ("a_fenv", 0.6f)
                         .s ("a_fa", 2.5f).s ("a_fd", 1).s ("a_fs", 1).s ("a_fr", 3).s ("a_aa", 0.003f).s ("a_ad", 3).s ("a_as", 0).s ("a_ar", 2.5f)
                         .s ("morph_y", 0.5f).s ("rv_mix", 0.4f).s ("rv_size", 0.8f).s ("p_gain", -8.4f).s ("veltrack", 0.8f).f);
            v.push_back (P ("Roj klávesy", "Klávesy").s ("a_wave", 8).s ("a_color", 0.6f).s ("a_uni", 4).s ("a_spread", 18).s ("a_level", 1).s ("a_fenv", 0.5f)
                         .s ("a_fa", 0.4f).s ("a_fd", 0.5f).s ("a_fs", 1).s ("a_fr", 0.8f).s ("a_aa", 0.005f).s ("a_ad", 1.4f).s ("a_as", 0.25f).s ("a_ar", 0.8f)
                         .s ("morph_y", -0.15f).s ("rv_mix", 0.3f).s ("rv_size", 0.6f).s ("p_gain", -7.7f).s ("veltrack", 0.75f).f);
            v.push_back (P ("Roj lead", "Leady").s ("a_wave", 8).s ("a_color", 0.9f).s ("a_uni", 2).s ("a_spread", 8).s ("a_level", 0.9f).s ("a_fenv", 0.3f)
                         .s ("a_fa", 0.25f).s ("a_fd", 0.5f).s ("a_fs", 1).s ("a_fr", 0.4f).s ("a_aa", 0.03f).s ("a_ad", 0.5f).s ("a_as", 0.8f).s ("a_ar", 0.3f)
                         .s ("a_lrate", 5.2f).s ("a_ldepth", 0.12f).s ("a_ldelay", 0.3f).s ("mono", 1).s ("glide", 0.05f)
                         .s ("morph_y", 0.6f).s ("rv_mix", 0.3f).s ("rv_size", 0.6f).s ("p_gain", -5.9f).s ("veltrack", 0.6f).f);
            v.push_back (P ("Roj oblak", "Pady").s ("a_wave", 8).s ("a_color", 0.85f).s ("a_uni", 6).s ("a_spread", 28).s ("a_level", 0.8f).s ("a_fenv", 1)
                         .s ("a_fa", 5).s ("a_fd", 1).s ("a_fs", 1).s ("a_fr", 15).s ("a_aa", 2).s ("a_ad", 2).s ("a_as", 0.9f).s ("a_ar", 3.5f)
                         .s ("ch_on", 1).s ("ch_mix", 0.3f).s ("rv_mix", 0.5f).s ("rv_size", 0.9f).s ("rv_pre", 30).s ("p_gain", -6.4f).s ("veltrack", 0.6f).f);
            // Wavetable (wave 9): Farba = Pozicia v tabulke, a_wt = tabulka, Obalka (filtra) a LFO ciel "Farba" hybu poziciou
            // vocoder: jasne, siroke nosice s rychlym nastupom (spev z inej stopy cez "Hlas pre vocoder")
            v.push_back (P ("Vocoder Robo zbor", "Pady").s ("a_wave", 2).s ("a_uni", 6).s ("a_spread", 16).s ("a_level", 0.8f)
                         .s ("a_aa", 0.01f).s ("a_ad", 0.5f).s ("a_as", 1.0f).s ("a_ar", 0.35f)
                         .s ("b_on", 1).s ("b_wave", 4).s ("b_level", 0.12f).s ("b_aa", 0.005f).s ("b_as", 1.0f).s ("b_ar", 0.3f)
                         .s ("vc_on", 1).s ("vc_bands", 20).s ("vc_clarity", 0.45f).s ("vc_release", 80).s ("vc_dry", 0.15f).s ("vc_eqh", -4.0f)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.35f).s ("rv_mix", 0.3f).s ("rv_size", 0.7f).s ("p_gain", -6.0f).f);
            v.push_back (P ("Vocoder Daft", "Leady").s ("a_wave", 3).s ("a_uni", 2).s ("a_spread", 8).s ("a_level", 0.85f)
                         .s ("a_aa", 0.003f).s ("a_ad", 0.3f).s ("a_as", 1.0f).s ("a_ar", 0.15f).s ("glide", 0.04f)
                         .s ("vc_on", 1).s ("vc_bands", 14).s ("vc_clarity", 0.4f).s ("vc_release", 45).s ("vc_dry", 0.05f).s ("vc_eqh", -4.0f)
                         .s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.15f).s ("dl_fb", 0.25f).s ("rv_mix", 0.18f).s ("p_gain", -6.0f).f);
            v.push_back (P ("Vocoder Vesmír", "Pady").s ("a_wave", 9).s ("a_wt", 3).s ("a_color", 0.3f).s ("a_uni", 6).s ("a_spread", 14).s ("a_level", 0.8f)
                         .s ("a_aa", 0.02f).s ("a_ad", 1.0f).s ("a_as", 1.0f).s ("a_ar", 1.2f)
                         .s ("vc_on", 1).s ("vc_bands", 26).s ("vc_formant", -2).s ("vc_clarity", 0.5f).s ("vc_release", 140).s ("vc_dry", 0.12f).s ("vc_eqh", -4.0f)
                         .s ("ch_on", 1).s ("ch_mix", 0.4f).s ("rv_mix", 0.5f).s ("rv_size", 0.9f).s ("rv_pre", 30).s ("p_gain", -6.0f).f);
            v.push_back (P ("Vocoder Sklo", "Pady").s ("a_wave", 9).s ("a_wt", 6).s ("a_color", 0.5f).s ("a_uni", 4).s ("a_spread", 10).s ("a_level", 0.8f)
                         .s ("a_aa", 0.01f).s ("a_as", 1.0f).s ("a_ar", 0.6f)
                         .s ("vc_on", 1).s ("vc_bands", 32).s ("vc_formant", 3).s ("vc_clarity", 0.5f).s ("vc_release", 70).s ("vc_dry", 0.2f).s ("vc_eqh", -4.0f)
                         .s ("ch_on", 1).s ("ch_mix", 0.3f).s ("rv_mix", 0.35f).s ("rv_size", 0.75f).s ("p_gain", -6.0f).f);
            v.push_back (P ("WT Vokál pad", "Pady").s ("a_wave", 9).s ("a_wt", 3).s ("a_color", 0.12f).s ("a_uni", 5).s ("a_spread", 13).s ("a_level", 0.8f)
                         .s ("a_cut", 6500).s ("a_reso", 0.12f).s ("a_fenv", 0.22f).s ("a_fa", 2.2f).s ("a_fd", 3).s ("a_fs", 0.5f).s ("a_fr", 2.5f)
                         .s ("a_aa", 0.9f).s ("a_ad", 1.5f).s ("a_as", 0.9f).s ("a_ar", 2.2f).s ("a_ltgt", 4).s ("a_lshape", 1).s ("a_lrate", 0.11f).s ("a_ldepth", 0.55f)
                         .s ("b_on", 1).s ("b_wave", 9).s ("b_wt", 7).s ("b_color", 0.45f).s ("b_oct", 1).s ("b_uni", 3).s ("b_spread", 9).s ("b_level", 0.3f)
                         .s ("b_aa", 1.2f).s ("b_ad", 1.5f).s ("b_as", 0.9f).s ("b_ar", 2.4f).s ("b_ltgt", 4).s ("b_lrate", 0.07f).s ("b_ldepth", 0.4f)
                         .s ("ch_on", 1).s ("ch_type", 1).s ("ch_mix", 0.4f).s ("rv_mix", 0.45f).s ("rv_size", 0.85f).s ("rv_pre", 20).s ("p_gain", -12.7f).s ("veltrack", 0.6f).f);
            v.push_back (P ("WT Digitál lead", "Leady").s ("a_wave", 9).s ("a_wt", 4).s ("a_color", 0.3f).s ("a_uni", 3).s ("a_spread", 7).s ("a_level", 0.85f)
                         .s ("a_fenv", 0.3f).s ("a_fa", 0.002f).s ("a_fd", 0.7f).s ("a_fs", 0.25f).s ("a_fr", 0.3f)
                         .s ("a_aa", 0.008f).s ("a_ad", 0.4f).s ("a_as", 0.9f).s ("a_ar", 0.25f).s ("a_lrate", 5.4f).s ("a_ldepth", 0.1f).s ("a_ldelay", 0.3f)
                         .s ("mono", 1).s ("glide", 0.05f).s ("dl_on", 1).s ("dl_sync", 2).s ("dl_mix", 0.18f).s ("dl_fb", 0.3f).s ("rv_mix", 0.22f).s ("rv_size", 0.6f)
                         .s ("p_gain", -9.1f).s ("veltrack", 0.6f).f);
            v.push_back (P ("WT Analog bas", "Basy").s ("a_wave", 9).s ("a_wt", 0).s ("a_color", 0.08f).s ("a_uni", 2).s ("a_spread", 6).s ("a_level", 0.8f)
                         .s ("a_cut", 700).s ("a_reso", 0.22f).s ("a_fenv", 0.4f).s ("a_fa", 0.002f).s ("a_fd", 0.32f).s ("a_fs", 0.15f).s ("a_fr", 0.12f)
                         .s ("a_aa", 0.003f).s ("a_ad", 0.5f).s ("a_as", 0.85f).s ("a_ar", 0.12f)
                         .s ("b_on", 1).s ("b_wave", 0).s ("b_oct", -1).s ("b_level", 0.55f).s ("b_aa", 0.003f).s ("b_ad", 0.5f).s ("b_as", 0.85f).s ("b_ar", 0.12f)
                         .s ("mono", 1).s ("glide", 0.03f).s ("tube", 0.35f).s ("rv_on", 0).s ("p_gain", -10).s ("veltrack", 0.5f).f);
            return v;
        }();
        return list;
    }

    const std::vector<ParamSpec>& paramSpecs()
    {
        static const std::vector<ParamSpec> specs = build();
        return specs;
    }

    const ParamSpec* findSpec (const juce::String& id)
    {
        static const std::map<juce::String, const ParamSpec*> m = []
        {
            std::map<juce::String, const ParamSpec*> r;
            for (auto& s : paramSpecs()) r[s.id] = &s;
            return r;
        }();
        auto it = m.find (id);
        return it != m.end() ? it->second : nullptr;
    }

    bool isPatchParam (const juce::String& id)
    {
        static const juce::StringArray macros { "preset", "bright", "attack", "release", "space", "width", "volume", "x_rnd", "x_mut" };
        return ! macros.contains (id);
    }

    float ratioValue (int index)
    {
        static const float r[] = { 0.5f, 1, 1.5f, 2, 3, 3.5f, 4, 5, 7, 14 };
        return r[juce::jlimit (0, 9, index)];
    }

    juce::String formatValue (const ParamSpec& s, float v)
    {
        if (s.kind == 1) return T (s.choices[juce::jlimit (0, s.choices.size() - 1, juce::roundToInt (v))]);
        if (s.kind == 2) return v > 0.5f ? T ("Zap") : T ("Vyp");
        const auto& u = s.unit;
        if (u == "%") return juce::String (juce::roundToInt (v * (s.hi > 1.5f ? 100.0f : 100.0f))) + " %";
        if (u == "bip") return juce::String (juce::roundToInt (v * 100.0f)) + " %";
        if (u == "pan") return std::abs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
        if (u == "s") return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
        if (u == "ms") return juce::String (juce::roundToInt (v)) + " ms";
        if (u == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : (v < 10 ? juce::String (v, 2) : juce::String (juce::roundToInt (v))) + " Hz";
        if (u == "ct") return (v > 0.05f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
        if (u == "st" || u == "oct") return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v));
        if (u == "dB") return juce::String (v, 1) + " dB";
        if (u == "x") return juce::String (juce::roundToInt (v));
        if (u == "note") return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 4);
        return juce::String (v, 2);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
        for (const auto& s : paramSpecs())
        {
            const juce::ParameterID pid { s.id, 2 };
            if (s.id == "release") { v.push_back (std::make_unique<juce::AudioParameterFloat> (pid, T ("Dozvuk", "Release"), juce::NormalisableRange<float> (s.lo, s.hi), s.def,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; }))); continue; }
            if (s.kind == 1)
            {
                juce::StringArray ch; for (auto& c : s.choices) ch.add (T (c));
                v.push_back (std::make_unique<juce::AudioParameterChoice> (pid, T (s.name), ch, (int) s.def));
            }
            else if (s.kind == 2) v.push_back (std::make_unique<juce::AudioParameterBool> (pid, T (s.name), s.def > 0.5f));
            else
            {
                juce::NormalisableRange<float> r (s.lo, s.hi);
                if (s.unit == "x" || s.unit == "st" || s.unit == "oct" || s.unit == "note") r.interval = 1.0f;
                if (s.skew != 1.0f) r.skew = s.skew;
                const ParamSpec* sp = &s;
                v.push_back (std::make_unique<juce::AudioParameterFloat> (pid, T (s.name), r, s.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([sp] (float x, int) { return formatValue (*sp, x); })));
            }
        }
        // skryty parameter pre hostitela: zvysi sa po kazdom "Inspiracia" zachyteni (stav obsahuje <capture>)
        v.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "capture_seq", 2 }, "Capture", 0, 1000000, 0,
                                                                juce::AudioParameterIntAttributes().withAutomatable (false)));
        // skryty parameter pre hostitela: "Nauc sa zvuk" - Korgus Sequencer otvori okno ucenia zvuku z inej stopy
        v.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "learn_req", 3 }, "Learn", 0, 1000000, 0,
                                                                juce::AudioParameterIntAttributes().withAutomatable (false)));
        return { v.begin(), v.end() };
    }
}
