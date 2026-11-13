#include "Lang.h"
#include <unordered_map>
#include <string>

namespace nova
{
    namespace
    {
        // slovensky text -> anglicky
        const std::unordered_map<std::string, const char*>& table()
        {
            static const std::unordered_map<std::string, const char*> t = {
                // vseobecne
                { " (kópia)", " (copy)" }, { " (môj)", " (mine)" }, { " hlasov", " voices" }, { "(žiadny zvuk)", "(no sound)" },
                { "+ Pridať", "+ Add" }, { "Zap", "On" }, { "Vyp", "Off" }, { "Zrušiť", "Cancel" }, { "Uložiť", "Save" }, { "Uložiť…", "Save…" },
                { "Späť", "Undo" }, { "Znova", "Redo" }, { "Načítať", "Load" }, { "Premenovať", "Rename" }, { "Premenovať…", "Rename…" },
                { "Duplikovať", "Duplicate" }, { "Exportovať…", "Export…" }, { "Zmazať", "Delete" }, { "Vytvoriť", "Create" }, { "Názov", "Name" },
                { "Nový názov", "New name" }, { "Jednoduchá", "Simple" }, { "Rozšírená", "Advanced" }, { "Rozšírená ▸", "Advanced ▸" },
                { "Veľkosť", "Size" }, { "Hlasitosť", "Volume" }, { "Prekvap ma", "Surprise me" }, { "Jazyk", "Language" },
                { "Vymyslí úplne nový zvuk. Stlač znova pre ďalší, Späť vráti predošlý.", "Invents a brand-new sound. Press again for another, Undo goes back." },
                { "Vymyslí úplne nový zvuk. Stlač znova pre ďalší. Ak sa páči, ulož ho v rozšírenej verzii.", "Invents a brand-new sound. Press again for another. If you like it, save it in the advanced view." },
                { "Úplný editor zvuku: vrstvy, filtre, efekty, arpeggiátor, vlastné zvuky", "Full sound editor: layers, filters, effects, arpeggiator, your own sounds" },
                { "Klavír: Salamander Grand Piano V3 – Alexander Holm, licencia CC-BY 3.0", "Piano: Salamander Grand Piano V3 – Alexander Holm, licence CC-BY 3.0" },
                { "Klavírne zvuky sa nepodarilo načítať", "Could not load the piano sounds" }, { "Načítavam klavír…", "Loading piano…" },
                { "Súbor sa nedá prečítať (WAV, AIFF, FLAC, MP3, OGG).", "Can't read the file (WAV, AIFF, FLAC, MP3, OGG)." },
                { "Zvuk sa nepodarilo spracovať.", "Couldn't process the sound." }, { "Nový zvuk", "New sound" }, { "Zvuk", "Sound" },
                // zoznam zvukov
                { "ZVUKY", "SOUNDS" }, { "Hľadať zvuk…", "Search sounds…" }, { "Import zvuku…", "Import sound…" }, { "Import zvuku", "Import sound" },
                { "Pridať zvuk zo súboru .knova (napr. z iného počítača)", "Add a sound from a .knova file (e.g. from another computer)" },
                { "Nový priečinok na zvuky, nový zvuk, zvuk z WAV, import zvukov", "New sound folder, new sound, sound from WAV, import sounds" },
                { "Nový priečinok na zvuky…", "New sound folder…" }, { "Nový priečinok na zvuky", "New sound folder" },
                { "Nový zvuk (začať od základu)", "New sound (start from scratch)" }, { "Zvuk z nahrávky WAV…", "Sound from a WAV recording…" },
                { "Zvuk z nahrávky", "Sound from a recording" }, { "Import zvukov (.knova)…", "Import sounds (.knova)…" },
                { "Otvoriť priečinok so zvukmi na disku", "Open the sound folder on disk" }, { "Moje pady", "My pads" },
                { "Premenovať priečinok…", "Rename folder…" }, { "Premenovať priečinok", "Rename folder" },
                { "Zmazať priečinok (zvuky ostanú v „Moje zvuky“)", "Delete folder (sounds go to \"My sounds\")" },
                { "Priečinok „", "Folder \"" }, { "“ už existuje.", "\" already exists." }, { "Presunúť do priečinka", "Move to folder" },
                { "Exportovať zvuk", "Export sound" }, { "Zmazať zvuk", "Delete sound" }, { "Naozaj zmazať zvuk „", "Really delete the sound \"" }, { "“?", "\"?" },
                { "Premenovať zvuk", "Rename sound" }, { "Uložiť zvuk", "Save sound" }, { "Zvuk sa uloží medzi vlastné zvuky.", "The sound is saved among your own sounds." },
                { "Priečinok", "Folder" }, { "Všetko", "All" },
                { "Ťahaním myšou meníš poradie, pustením na priečinok ho tam presunieš. Pravé tlačidlo = ďalšie možnosti.", "Drag to reorder, drop on a folder to move it there. Right-click = more options." },
                // kategorie
                { "Klavír", "Piano" }, { "Klávesy", "Keys" }, { "Pady", "Pads" }, { "Sláčiky", "Strings" }, { "Plucky", "Plucks" },
                { "Zvonky", "Bells" }, { "Basy", "Bass" }, { "Leady", "Leads" }, { "Moje zvuky", "My sounds" },
                // tovarenske zvuky
                { "Koncertné piano", "Concert Piano" }, { "Mäkké piano", "Soft Piano" }, { "Elektrické piano", "Electric Piano" },
                { "Teplý pad", "Warm Pad" }, { "Sklenený pad", "Glass Pad" }, { "Husle", "Violin" }, { "Pluck", "Pluck" },
                { "Harfa", "Harp" }, { "Celesta", "Celesta" }, { "Teplý bas", "Warm Bass" }, { "Sub bas", "Sub Bass" },
                { "Mäkký lead", "Soft Lead" }, { "Organ", "Organ" },
                // vrstvy
                { "VRSTVA A", "LAYER A" }, { "VRSTVA B", "LAYER B" }, { "Vrstva A", "Layer A" }, { "Vrstva B", "Layer B" },
                { "Sín|Troj|Píla|Obdĺž|Šum|Pluck|FM|Sampel", "Sine|Tri|Saw|Square|Noise|Pluck|FM|Sample" },
                { "Oktáva", "Octave" }, { "Poltón", "Semi" }, { "Jemne", "Fine" }, { "Unison", "Unison" }, { "Rozlad.", "Spread" },
                { "Úroveň", "Level" }, { "Panoráma", "Pan" }, { "Cutoff", "Cutoff" }, { "Rezon.", "Reso" }, { "Obálka", "Env amt" },
                { "Nábeh", "Attack" }, { "Pokles", "Decay" }, { "Držanie", "Sustain" }, { "Doznenie", "Release" }, { "Rýchlosť", "Rate" },
                { "Hĺbka", "Depth" }, { "Onesk.", "Delay" }, { "Farba", "Color" }, { "Šírka pulzu", "Pulse width" }, { "Farba šumu", "Noise color" },
                { "Jas struny", "Brightness" }, { "FM index", "FM index" }, { "Tvrdosť", "Hardness" }, { "Zákl. tón", "Root key" },
                { "FM pomer", "FM ratio" }, { "Načítať WAV…", "Load WAV…" }, { "Opakovať", "Loop" }, { "Zvuk do vrstvy", "Sound for the layer" },
                { "Vlastný zvuk do vrstvy (alebo pretiahni WAV súbor myšou na vrstvu)", "Your own sound for the layer (or drag a WAV file onto the layer)" },
                { "Klavír = vstavané nahrávky klavíra, Vlastný WAV = tvoj zvuk", "Piano = built-in piano recordings, Own WAV = your sound" },
                { "OBÁLKA FILTRA", "FILTER ENVELOPE" }, { "OBÁLKA HLASITOSTI", "AMP ENVELOPE" },
                // tube
                { "Analóg", "Analog" }, { "Kompresia", "Compression" }, { "Tube komp", "Tube comp" },
                { "Teplo a jemné\n„plávanie“ ladenia", "Warmth and gentle\npitch drift" }, { "Vyrovná hlasité tóny", "Evens out loud notes" }, { "Stíšenie ", "Reduction " },
                // efekty
                { "EFEKTY", "EFFECTS" }, { "Sila", "Amount" }, { "Opakov.", "Repeats" }, { "Mix", "Mix" }, { "Čas", "Time" }, { "Tlmenie", "Damp" },
                { "Šírka", "Width" }, { "Úder", "Punch" }, { "Stredy", "Mid" }, { "Stredy Hz", "Mid freq" }, { "Výšky", "High" }, { "Dozvuk", "Reverb" },
                { "EQ len pre vrstvu A", "EQ for layer A only" }, { "EQ len pre vrstvu B", "EQ for layer B only" }, { "EQ celého zvuku", "EQ for the whole sound" },
                { "EQ len pre dozvuk (napr. ubrať basy, aby dozvuk nehučal)", "EQ for the reverb only (e.g. cut the lows so the reverb doesn't boom)" },
                { "Plochý", "Flat" }, { "Plochý EQ", "Flat EQ" }, { "Vynulovať tento EQ", "Reset this EQ" },
                { "Ťahaj body v grafe:\nhore / dole = hlasnejšie alebo tichšie,\nstredný aj vľavo / vpravo.\nDvojklik na bod ho vynuluje.",
                  "Drag the points on the graph:\nup / down = louder or quieter,\nthe middle one also left / right.\nDouble-click a point to reset it." },
                { "Ťahaj body: hore/dole = hlasnejšie/tichšie, stredný aj vľavo/vpravo. Dvojklik = späť na 0.", "Drag the points: up/down = louder/quieter, the middle one also left/right. Double-click = back to 0." },
                // arpeggiator, hlas
                { "ARPEGGIÁTOR", "ARPEGGIATOR" }, { "Držať", "Hold" }, { "VZOR", "PATTERN" }, { "RÝCHLOSŤ", "SPEED" },
                { "Hore|Dole|H/D|Hrám|Náhod.|Akord", "Up|Down|Up/Dn|Played|Random|Chord" }, { "Oktávy", "Octaves" }, { "Dĺžka", "Length" },
                { "Citlivosť", "Velocity" }, { "Ako veľmi hlasitosť závisí od sily úderu", "How much the volume depends on how hard you play" },
                { "Hrá vždy len jeden tón (basy, leady) – s Glide sa tóny prelínajú", "Plays one note at a time (bass, leads) – with Glide the notes slide" },
                // morph, master
                { "jasný", "bright" }, { "tmavý", "dark" },
                { "Vľavo vrstva A, vpravo vrstva B · hore jasnejší, dole tmavší · dvojklik = stred", "Left layer A, right layer B · up brighter, down darker · double-click = centre" },
                { "Dolná|Horná", "Low|High" }, { "Hlasitosť tohto zvuku (ukladá sa so zvukom)", "Volume of this sound (saved with the sound)" },
                // nazvy parametrov
                { "Jas", "Brightness" }, { "Priestor", "Space" }, { "Priebeh", "Wave" }, { "Rozladenie", "Spread" }, { "Filter", "Filter" },
                { "Rezonancia", "Resonance" }, { "LFO tvar", "LFO shape" }, { "LFO cieľ", "LFO target" }, { "Oneskorenie", "LFO delay" },
                { "Zdroj", "Source" }, { "Základný tón", "Root key" }, { "Drive", "Drive" }, { "Chorus", "Chorus" }, { "Delay", "Delay" },
                { "Synchro", "Sync" }, { "Opakovanie", "Feedback" }, { "Ping-pong", "Ping-pong" }, { "EQ basy", "EQ low" }, { "EQ stredy", "EQ mid" },
                { "EQ stredy Hz", "EQ mid freq" }, { "EQ výšky", "EQ high" }, { "Arpeggiátor", "Arpeggiator" }, { "Vzor", "Pattern" },
                { "Mono", "Mono" }, { "Glide", "Glide" }, { "Morph A–B", "Morph A–B" }, { "Tmavý–Jasný", "Dark–Bright" }, { "Master filter", "Master filter" },
                { "Typ", "Type" }, { "Úroveň zvuku", "Sound level" },
                // volby
                { "Sínus|Trojuholník|Píla|Obdĺžnik|Šum|Pluck|FM|Sampel", "Sine|Triangle|Saw|Square|Noise|Pluck|FM|Sample" },
                { "Sínus", "Sine" }, { "Trojuholník", "Triangle" }, { "Píla", "Saw" }, { "Obdĺžnik", "Square" }, { "Šum", "Noise" }, { "Sampel", "Sample" },
                { "Dolná", "Low-pass" }, { "Pásmová", "Band-pass" }, { "Horná", "High-pass" }, { "Telo", "Body" },
                { "Troj.", "Tri" }, { "Obdĺž.", "Square" }, { "Náhod.", "Random" }, { "Výška", "Pitch" }, { "Hlasit.", "Volume" }, { "Panor.", "Pan" },
                { "Vlastný WAV", "Own WAV" }, { "Voľne", "Free" }, { "1/8 bodka", "1/8 dotted" }, { "1/8 triola", "1/8 triplet" },
                { "Hore", "Up" }, { "Dole", "Down" }, { "Hore/Dole", "Up/Down" }, { "Ako hrám", "As played" }, { "Náhodne", "Random" }, { "Akord", "Chord" },
            };
            return t;
        }

        int& langRef() { static int l = -1; return l; }
        juce::File langFile() { return dataDir().getChildFile ("jazyk.txt"); }
    }

    juce::File dataDir()
    {
        static const juce::File d = []
        {
            const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
            const auto nd = docs.getChildFile ("Korgus Inspire"), od = docs.getChildFile ("Korgus Nova");
            if (! nd.exists() && od.isDirectory()) od.moveFileTo (nd);
            return nd;
        }();
        return d;
    }

    int lang()
    {
        auto& l = langRef();
        if (l < 0)
        {
            const auto s = langFile().loadFileAsString().trim();
            if (s == "sk") l = 0;
            else if (s == "en") l = 1;
            else
            {
                const auto ul = juce::SystemStats::getUserLanguage().toLowerCase();
                l = (ul.startsWith ("sk") || ul.startsWith ("cs")) ? 0 : 1;
            }
        }
        return l;
    }

    void setLang (int l)
    {
        langRef() = l == 1 ? 1 : 0;
        langFile().getParentDirectory().createDirectory();
        langFile().replaceWithText (l == 1 ? "en" : "sk");
    }

    juce::String T (const char* sk)
    {
        if (lang() == 1)
        {
            auto it = table().find (sk);
            if (it != table().end()) return juce::String::fromUTF8 (it->second);
        }
        return juce::String::fromUTF8 (sk);
    }

    juce::String T (const juce::String& sk)
    {
        if (lang() == 1)
        {
            auto it = table().find (sk.toStdString());
            if (it != table().end()) return juce::String::fromUTF8 (it->second);
        }
        return sk;
    }
}
