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
                // prepinace s ikonami, tvarovanie zvuku
                { "Klavír|Vzorky|WAV", "Piano|Samples|WAV" }, { "Nástroj|Vzorky|WAV", "Instrument|Samples|WAV" },
                { "Realistické nahraté nástroje – klavír, Rhodes, gitary, basy, sláčiky, dychy, mallets", "Realistic recorded instruments – piano, Rhodes, guitars, basses, strings, winds, mallets" },
                { "Jednoduché vzorky – organ, zbor, zvon, synth…", "Simple samples – organ, choir, bell, synth…" },
                { "Klik = zoznam nástrojov", "Click = list of instruments" }, { " (načítavam…)", " (loading…)" },
                { "Gitary", "Guitars" }, { "Dychy", "Winds" }, { "Synth klávesy", "Synth Keys" }, { "Synth sláčiky", "Synth Strings" }, { "Synth basy", "Synth Bass" }, { "Nástroje", "Instruments" }, { "Syntezátor", "Synthesizer" },
                { "Koncertný klavír", "Concert Grand" }, { "Pianíno", "Upright Piano" }, { "Rhodes", "Rhodes" }, { "Čembalo", "Harpsichord" },
                { "Akustická gitara", "Acoustic Guitar" }, { "Jazzová gitara", "Jazz Guitar" }, { "Basgitara", "Bass Guitar" }, { "Jazzový kontrabas", "Jazz Upright Bass" },
                { "Sláčiková sekcia", "String Section" }, { "Sólové husle", "Solo Violin" }, { "Violončelá", "Cellos" }, { "Trúbka s dusítkom", "Muted Trumpet" },
                { "Trúbka", "Trumpet" }, { "Tenor saxofón", "Tenor Sax" }, { "Trombón", "Trombone" }, { "Priečna flauta", "Concert Flute" }, { "Klarinet", "Clarinet" },
                { "Jazzový vibrafón", "Jazz Vibraphone" }, { "Koncertná marimba", "Concert Marimba" }, { "Koncertná celesta", "Concert Celesta" }, { "Zvonkohra", "Glockenspiel" },
                { "Pizzicato sláčiky", "Pizzicato Strings" }, { "Sláčiky tremolo", "Tremolo Strings" }, { "Sláčiky staccato", "Staccato Strings" }, { "Koncertná harfa", "Concert Harp" },
                { "Píšťalový organ", "Pipe Organ" }, { "Elektrická gitara", "Electric Guitar" }, { "Ústna harmonika", "Harmonica" }, { "Lesný roh", "French Horn" },
                { "Hoboj", "Oboe" }, { "Fagot", "Bassoon" }, { "Tuba", "Tuba" }, { "Okarína", "Ocarina" }, { "Zobcová flauta", "Recorder" },
                { "Africká kalimba", "African Kalimba" }, { "Rúrkové zvony", "Tubular Bells" }, { "Xylofón", "Xylophone" },
                { "Klavír: Steinway B z VCSL (Versilian Studios, CC0), pôvodný Salamander (A. Holm, CC BY 3.0) · Rhodes: jRhodes3 (J. Learman, CC BY-NC 4.0) · ostatné: Versilian Studios, Karoryfer (CC0)",
                  "Piano: Steinway B from VCSL (Versilian Studios, CC0), original Salamander (A. Holm, CC BY 3.0) · Rhodes: jRhodes3 (J. Learman, CC BY-NC 4.0) · others: Versilian Studios, Karoryfer (CC0)" }, { "Načítať…", "Load…" }, { "Klik = zoznam vzoriek", "Click = list of samples" },
                { "Predchádzajúci zvuk", "Previous sound" }, { "Ďalší zvuk", "Next sound" },
                { "LFO podľa tempa piesne (1/8 = vlnenie každú osminu, T = triola). Voľne = podľa gombíka Rýchlosť.", "LFO follows the song tempo (1/8 = a wobble every eighth, T = triplet). Free = set by the Rate knob." },
                { "LFO tempo", "LFO tempo" },
                { "Vzorky ▾", "Samples ▾" }, { "Zabudované vzorky nástrojov – gitary, basy, sláčiky, dychy, zbor…", "Built-in instrument samples – guitars, basses, strings, winds, choir…" },
                { "Predchádzajúci zvuk (z priečinka alebo zo vzoriek)", "Previous sound (from the folder or the samples)" }, { "Ďalší zvuk (z priečinka alebo zo vzoriek)", "Next sound (from the folder or the samples)" },
                { "Vocoder Robo zbor", "Vocoder Robo Choir" }, { "Vocoder Vesmír", "Vocoder Space" }, { "Vocoder Sklo", "Vocoder Glass" },
                { "Flauta", "Flute" }, { "Ukážka", "Preview" }, { "pôvodné", "original" }, { "tmavší", "darker" }, { "jasnejší", "brighter" }, { "rýchlejší", "faster" }, { "pomalší", "slower" }, { "kratší", "shorter" }, { "dlhší", "longer" }, { "suchší", "drier" }, { "priestornejší", "roomier" }, { "užšia", "narrower" }, { "širšia", "wider" },
                { "Zachytí, čo si práve zahral, aj bez nahrávania – v Korgus Sequenceri to vloží do stopy.", "Captures what you just played, even without recording – in Korgus Sequencer it puts it on a track." }, { "Editor samplu", "Sample editor" }, { "Prehrať", "Play" }, { "Odzadu", "Reverse" }, { "Slučka", "Loop" }, { "Zistiť tón", "Detect pitch" }, { "Nájsť slučku", "Find loop" }, { "So slučkou hrať od začiatku", "With loop, play from the start" }, { "Výber je na slučku príliš krátky.", "The selection is too short for a loop." }, { "Slučku sa nepodarilo nájsť.", "No loop point found." }, { "Slučka nájdená – zhoda ", "Loop found – match " }, { "Ďalšie: Nájsť slučku, Zistiť tón, Uložiť výsek, Pôvodné", "More: Find loop, Detect pitch, Save cut, Original" }, { "Pôvodné", "Original" }, { "Uložiť výsek", "Save cut" }, { "Uloží vybranú časť ako nový zvuk (WAV) do priečinka Moje samply – aj s nábehom, doznením a slučkou", "Saves the selected part as a new sound (WAV) in the My samples folder – with fade in, fade out and loop" }, { "Výsek sa nepodarilo uložiť.", "The cut could not be saved." }, { "Uložené: ", "Saved: " }, { "Moje samply", "My samples" }, { "Veľkosť okna", "Window size" }, { "Plnosť", "Fullness" }, { "Lesk", "Air" }, { "Doplní jemné výšky (vzduch) – hlavne nahrávkam s orezanými výškami, napr. klavíru", "Adds gentle highs (air) – mainly for recordings with cut highs, e.g. the piano" }, { "Ťahaj body: hore / dole = hlasnejšie / tichšie, vľavo / vpravo = výška. Pravé tlačidlo (alebo Shift) + ťahanie hore / dole = užšie / širšie pásmo, rovnako koliesko. Dvojklik = vynulovať.", "Drag points: up / down = louder / quieter, left / right = frequency. Right button (or Shift) + drag up / down = narrower / wider band, the wheel too. Double-click = reset." },
                { "Gitara s trsatkom", "Picked Guitar" }, { "Elektronický tom", "Electronic Tom" }, { "Kopák", "Kick" },
                { "Laserový lead", "Laser Lead" }, { "Vzdušný pad", "Airy Pad" },
                { "Chorus = širší zvuk", "Chorus = wider sound" }, { "Ensemble = sláčikový ensemble", "Ensemble = string ensemble" },
                { "Flanger = „lietadlo“", "Flanger = \"jet plane\"" }, { "Phaser = „lietadlo“, jemnejšie", "Phaser = softer \"jet plane\"" },
                { "Tremolo = kolísanie hlasitosti", "Tremolo = volume wobble" }, { "Modulácia zap / vyp", "Modulation on / off" },
                { "Sínus|Trojuholník|Píla|Obdĺžnik|Šum|Pluck|FM|Sampel|Roj|WT", "Sine|Triangle|Saw|Square|Noise|Pluck|FM|Sample|Swarm|WT" },
                { "Dolná|Pásmová|Horná|Telo", "Low|Band|High|Body" }, { "Sínus|Trojuholník|Obdĺžnik|Náhodne", "Sine|Triangle|Square|Random" },
                { "Klavír|WAV", "Piano|WAV" },
                { "Analóg – sínus (mäkký, čistý)", "Analog – sine (soft, pure)" }, { "Analóg – trojuholník (jemný, flauta)", "Analog – triangle (gentle, flute)" },
                { "Analóg – píla (ostrý, struny, brass)", "Analog – saw (bright, strings, brass)" }, { "Analóg – obdĺžnik (dutý, klarinet)", "Analog – square (hollow, clarinet)" },
                { "Šum (dych, vietor, bicie)", "Noise (breath, wind, drums)" }, { "Pluck – brnknutá struna", "Pluck – plucked string" },
                { "FM – zvonivé a elektrické piano", "FM – bells and electric piano" }, { "Sampel – klavír alebo vlastný WAV", "Sample – piano or your own WAV" },
                { "Roj – veľa rozladených hlasov (supersaw)", "Swarm – many detuned voices (supersaw)" }, { "Wavetable – plynulý prechod medzi tvarmi vlny", "Wavetable – smooth morph between wave shapes" },
                { "Dolná priepusť – stmavuje (najčastejší)", "Low-pass – darkens (most common)" }, { "Pásmová priepusť – nechá len stred", "Band-pass – keeps only the middle" },
                { "Horná priepusť – zoslabí basy", "High-pass – thins out the bass" }, { "Telo – rezonancie ako drevo nástroja", "Body – resonances like an instrument's wood" },
                { "LFO sínus", "LFO sine" }, { "LFO trojuholník", "LFO triangle" }, { "LFO obdĺžnik", "LFO square" }, { "LFO náhodne", "LFO random" },
                { "Vstavané nahrávky klavíra", "Built-in piano recordings" }, { "Vlastný zvuk WAV", "Your own WAV sound" },
                { "Skok", "Jump" }, { "Čas", "Time" }, { "Kláves", "Key" }, { "Úder", "Velocity" },
                { "Skok výšky na začiatku tónu (v poltónoch) – úder bubna, trsatko, „pew“ zvuky", "Pitch jump at the start of the note (semitones) – drum hits, picks, \"pew\" sounds" },
                { "Ako rýchlo sa výška vráti na tón", "How fast the pitch returns to the note" },
                { "Vyššie tóny majú otvorenejší filter (0 = rovnako pre všetky)", "Higher notes get a more open filter (0 = same for all)" },
                { "Silnejší úder otvorí filter viac (0 = nezáleží na sile)", "Harder hits open the filter more (0 = velocity ignored)" },
                { "Primieša šum do vrstvy – dych pri flaute, škrabnutie trsatka, vzduch", "Mixes noise into the layer – flute breath, pick scrape, air" },
                { "Skok výšky", "Pitch jump" }, { "Podľa klávesu", "Key follow" }, { "Podľa úderu", "Velocity follow" },
                { "Hore|Dole|Hore/Dole|Ako hrám|Náhodne|Akord", "Up|Down|Up/Down|As played|Random|Chord" },
                { "Arpeggio hore", "Arpeggio up" }, { "Arpeggio dole", "Arpeggio down" }, { "Hore a dole", "Up and down" }, { "V poradí, ako hráš", "In the order you play" },
                { "Náhodné poradie", "Random order" }, { "Celý akord naraz v rytme", "Whole chord at once in rhythm" },
                { "Dolná priepusť", "Low-pass" }, { "Horná priepusť", "High-pass" },
                { "Voľne|1/4|1/8.|1/8|1/8T|1/16", "Free|1/4|1/8.|1/8|1/8T|1/16" },
                { "Voľne = čas v ms, ostatné = podľa tempa (1/8. = osminka s bodkou, T = triola)", "Free = time in ms, others follow the tempo (1/8. = dotted eighth, T = triplet)" },
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
                { "Zvuk sa nepodarilo spracovať.", "Couldn't process the sound." },
                { "Vocoder mix", "Vocoder mix" },
                { "Pomer syntezátor / vocoder: vľavo čistý syntezátor, vpravo len vocoder", "Synth / vocoder balance: left = clean synth, right = vocoder only" },
                { "EQ len pre vocoder (napr. ubrať výšky, ak sú sykavky ostré)", "EQ for the vocoder only (e.g. cut highs if sibilants are harsh)" },
                { "Hlas zo stopy…", "Voice from track…" },
                { "V piesni nie je audio stopa so spevom", "No audio track with vocals in the song" },
                { "Vyber stopu so spevom – Inspiria bude spievať jej slovami", "Pick the vocal track – Inspiria will sing its words" },
                { "Vyber stopu so spevom a hraj akordy – Inspiria zaspieva slovami zo spevu.\nVyskúšaj zvuky „Vocoder…“ v zozname zvukov.", "Pick the vocal track and play chords – Inspiria sings the words.\nTry the “Vocoder…” sounds in the sound list." },
                { "Vocoder stopa", "Vocoder track" },
                { "Vocoder: Inspiria spieva slovami zo spevovej stopy", "Vocoder: Inspiria sings the words of the vocal track" },
                { "Pásma", "Bands" },
                { "Zrozumiteľnosť", "Clarity" },
                { "Hlas", "Voice" },
                { "Menej pásiem = klasický robot, viac = zrozumiteľnejšie", "Fewer bands = classic robot, more = clearer" },
                { "Posun farby hlasu (nižšie = mohutnejší, vyššie = detský / robotický)", "Shifts the voice colour (lower = bigger, higher = childlike / robotic)" },
                { "Spoluhlásky S, T, K z pôvodného hlasu – aby bolo rozumieť slovám", "Consonants S, T, K from the original voice – so the words are clear" },
                { "Ako plynulo doznieva každé slovo", "How smoothly each word fades" },
                { "Trochu pôvodného hlasu pod vocoderom (človek + robot)", "A little of the original voice under the vocoder (human + robot)" },
                { "1. Zapni VOCODER.  2. V Sequenceri: pravý klik na túto stopu → Hlas pre vocoder → vyber spevovú stopu.\n3. Hraj akordy – Inspiria zaspieva slovami zo spevu.", "1. Turn on VOCODER.  2. In Sequencer: right-click this track → Voice for vocoder → pick the vocal track.\n3. Play chords – Inspiria sings the words of the vocal." },
                { "Hlas prichádza", "Voice is coming in" },
                { "Žiadny hlas – vyber spevovú stopu v Sequenceri", "No voice – pick the vocal track in Sequencer" },
                { "Vocoder pásma", "Vocoder bands" },
                { "Vocoder formant", "Vocoder formant" },
                { "Vocoder zrozumiteľnosť", "Vocoder clarity" },
                { "Vocoder dozvuk", "Vocoder release" },
                { "Vocoder hlas", "Vocoder voice" },
                { "Gater swing", "Gate swing" },
                { "Rytmus…", "Rhythm…" },
                { "Hotové rytmy – nastavia kroky vzoru", "Ready-made rhythms – set the pattern steps" },
                { "Gater", "Gater" },
                { "Gater rýchlosť", "Gate rate" },
                { "Gater hĺbka", "Gate depth" },
                { "Gater hladkosť", "Gate smoothness" },
                { "Rytmický gater: seká zvuk do rytmu podľa tempa (trance, pop pady)", "Rhythmic gater: chops the sound to the tempo (trance, pop pads)" },
                { "Modulačná matica: LFO, obálka, sila úderu… môže hýbať ľubovoľným cieľom", "Modulation matrix: LFO, envelope, velocity… can move any target" },
                { "Hladkosť", "Smooth" },
                { "Dĺžka jedného kroku vzoru", "Length of one pattern step" },
                { "Krok vzoru – klik zapne / vypne", "Pattern step – click to toggle" },
                { "Zapni GATER a klikaj na kroky: zlaté = zvuk hrá, tmavé = ticho.\nNajlepšie znie s padmi a dlhými tónmi.", "Turn on GATER and click the steps: gold = sound plays, dark = silence.\nWorks best with pads and long notes." },
                { "Množstvo", "Amount" },
                { "Zdroj modulácie", "Modulation source" },
                { "Čím sa bude hýbať", "What it moves" },
                { "Mod zdroj", "Mod source" },
                { "Mod cieľ", "Mod target" },
                { "Mod množstvo", "Mod amount" },
                { "Vypnuté", "Off" },
                { "Obálka filtra", "Filter envelope" },
                { "Obálka hlasitosti", "Amp envelope" },
                { "Sila úderu", "Velocity" },
                { "Mod koliesko", "Mod wheel" },
                { "Rytmus (gater)", "Rhythm (gater)" },
                { "Náhoda", "Random" },
                { "Cutoff A+B", "Cutoff A+B" },
                { "Úroveň A", "Level A" },
                { "Úroveň B", "Level B" },
                { "Úroveň A+B", "Level A+B" },
                { "Výška A", "Pitch A" },
                { "Výška B", "Pitch B" },
                { "Výška A+B", "Pitch A+B" },
                { "Farba A", "Color A" },
                { "Farba B", "Color B" },
                { "Farba A+B", "Color A+B" },
                { "Panoráma A+B", "Pan A+B" },
                { "Rezonancia A+B", "Resonance A+B" },
                { "Gater krok 1", "Gate step 1" },
                { "Gater krok 2", "Gate step 2" },
                { "Gater krok 3", "Gate step 3" },
                { "Gater krok 4", "Gate step 4" },
                { "Gater krok 5", "Gate step 5" },
                { "Gater krok 6", "Gate step 6" },
                { "Gater krok 7", "Gate step 7" },
                { "Gater krok 8", "Gate step 8" },
                { "Gater krok 9", "Gate step 9" },
                { "Gater krok 10", "Gate step 10" },
                { "Gater krok 11", "Gate step 11" },
                { "Gater krok 12", "Gate step 12" },
                { "Gater krok 13", "Gate step 13" },
                { "Gater krok 14", "Gate step 14" },
                { "Gater krok 15", "Gate step 15" },
                { "Gater krok 16", "Gate step 16" }, { "Nový zvuk", "New sound" }, { "Zvuk", "Sound" },
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
                { "Jasný klavír", "Bright Piano" }, { "Klavír v hale", "Hall Piano" }, { "Klavír a pad", "Piano & Pad" }, { "Lo-fi klavír", "Lo-fi Piano" }, { "Kalné EP", "Dusty EP" }, { "Zvonivé EP", "Bell EP" }, { "Synthwave klávesy", "Synthwave Keys" }, { "Funky klávesy", "Funky Keys" }, { "Rockový organ", "Rock Organ" }, { "Kostolný organ", "Church Organ" }, { "Ambientný oblak", "Ambient Cloud" }, { "Temný pad", "Dark Pad" }, { "Zimný pad", "Winter Pad" }, { "Synthwave pad", "Synthwave Pad" }, { "Sklenené zvony", "Glass Bells" }, { "Dych", "Breath" }, { "Pomalé sláčiky", "Slow Strings" }, { "Pizzicato", "Pizzicato" }, { "Violončelo", "Cello" }, { "Orchester", "Orchestra" }, { "Nylonová gitara", "Nylon Guitar" }, { "Kalimba", "Kalimba" }, { "Synth pluck", "Synth Pluck" }, { "Marimba", "Marimba" }, { "Pluck v ozvene", "Echo Pluck" }, { "Hudobná skrinka", "Music Box" }, { "Vibrafón", "Vibraphone" }, { "Zvony", "Tubular Bells" }, { "808 bas", "808 Bass" }, { "Acid bas", "Acid Bass" }, { "Synthwave bas", "Synthwave Bass" }, { "Kontrabas", "Upright Bass" }, { "Reese bas", "Reese Bass" }, { "Pílový lead", "Saw Lead" }, { "Píšťala", "Flute" }, { "Retro lead", "Retro Lead" }, { "Spevavý lead", "Singing Lead" }, { "Roj Mellotron", "Swarm Mellotron" }, { "Roj zvony", "Swarm Bells" }, { "Roj klávesy", "Swarm Keys" }, { "Roj lead", "Swarm Lead" }, { "Roj oblak", "Swarm Cloud" },
                // vrstvy
                { "VRSTVA A", "LAYER A" }, { "VRSTVA B", "LAYER B" }, { "Vrstva A", "Layer A" }, { "Vrstva B", "Layer B" },
                { "Sín|Troj|Píla|Obdĺž|Šum|Pluck|FM|Smp|Roj|WT", "Sine|Tri|Saw|Sqr|Noise|Pluck|FM|Smp|Swarm|WT" },
                { "Oktáva", "Octave" }, { "Poltón", "Semi" }, { "Jemne", "Fine" }, { "Unison", "Unison" }, { "Rozlad.", "Spread" },
                { "Úroveň", "Level" }, { "Panoráma", "Pan" }, { "Cutoff", "Cutoff" }, { "Rezon.", "Reso" }, { "Obálka", "Env amt" },
                { "Nábeh", "Attack" }, { "Pokles", "Decay" }, { "Držanie", "Sustain" }, { "Doznenie", "Release" }, { "Rýchlosť", "Rate" },
                { "Hĺbka", "Depth" }, { "Onesk.", "Delay" }, { "Farba", "Color" }, { "Šírka pulzu", "Pulse width" }, { "Farba šumu", "Noise color" },
                { "Jas struny", "Brightness" }, { "FM index", "FM index" }, { "Tvrdosť", "Hardness" }, { "Zákl. tón", "Root key" },
                { "FM pomer", "FM ratio" }, { "Načítať WAV…", "Load WAV…" }, { "Predchádzajúci zvuk z priečinka", "Previous sound in folder" }, { "Ďalší zvuk z priečinka", "Next sound in folder" }, { "Najprv načítaj zvuk zo súboru – potom prepínaš ďalšie z toho istého priečinka.", "Load a sound from a file first – then you can step through the others in the same folder." }, { "Opakovať", "Loop" }, { "Zvuk do vrstvy", "Sound for the layer" },
                { "Vlastný zvuk do vrstvy (alebo pretiahni WAV súbor myšou na vrstvu)", "Your own sound for the layer (or drag a WAV file onto the layer)" },
                { "Klavír = vstavané nahrávky klavíra, Vlastný WAV = tvoj zvuk", "Piano = built-in piano recordings, Own WAV = your sound" },
                { "OBÁLKA FILTRA", "FILTER ENVELOPE" }, { "OBÁLKA HLASITOSTI", "AMP ENVELOPE" },
                // tube
                { "Analóg", "Analog" }, { "Kompresia", "Compression" }, { "Tube komp", "Tube comp" },
                { "Teplo a jemné\n„plávanie“ ladenia", "Warmth and gentle\npitch drift" }, { "Vyrovná hlasité tóny", "Evens out loud notes" }, { "Stíšenie ", "Reduction " }, { "Skopírovať vrstvu A do vrstvy B", "Copy layer A to layer B" }, { "Skopírovať vrstvu B do vrstvy A", "Copy layer B to layer A" },
                // efekty
                { "EFEKTY", "EFFECTS" }, { "Sila", "Amount" }, { "Opakov.", "Repeats" }, { "Mix", "Mix" }, { "Čas", "Time" }, { "Tlmenie", "Damp" },
                { "Šírka", "Width" }, { "Úder", "Punch" }, { "Stredy", "Mid" }, { "Stredy Hz", "Mid freq" }, { "Výšky", "High" }, { "Dozvuk", "Reverb" },
                { "EQ len pre vrstvu A", "EQ for layer A only" }, { "EQ len pre vrstvu B", "EQ for layer B only" }, { "EQ celého zvuku", "EQ for the whole sound" },
                { "EQ len pre dozvuk (napr. ubrať basy, aby dozvuk nehučal)", "EQ for the reverb only (e.g. cut the lows so the reverb doesn't boom)" },
                { "Zapnutý", "On" }, { "Vypnúť / zapnúť tento EQ (na porovnanie)", "Turn this EQ off / on (to compare)" }, { "EQ zapnutý", "EQ on" },
                { "Vymyslí úplne nový zvuk", "Invents a brand-new sound" }, { ". Stlač znova pre ďalší, Späť vráti predošlý. ▾ = vybrať druh zvuku.", ". Press again for another, Undo goes back. ▾ = choose the kind of sound." },
                { "Prekvap ma – druh zvuku", "Surprise me – kind of sound" }, { "Čokoľvek", "Anything" }, { "Vybrať druh zvuku pre Prekvap ma", "Choose the kind of sound for Surprise me" },
                { "Obmena", "Vary" }, { "Jemne pozmení aktuálny zvuk – stlač viackrát, Späť vráti predošlý", "Slightly changes the current sound – press several times, Undo goes back" },
                { " (obmena)", " (variation)" }, { "Odobrať z obľúbených", "Remove from favourites" }, { "Pridať medzi obľúbené", "Add to favourites" },
                { "Ťahaním myšou meníš poradie, pustením na priečinok ho tam presunieš. ☆ = obľúbený. Pravé tlačidlo = ďalšie možnosti.", "Drag to reorder, drop on a folder to move it there. ☆ = favourite. Right-click = more options." },
                { "EQ len pre opakovania delayu (napr. ubrať basy a výšky, aby delay nezahlcoval)", "EQ for the delay repeats only (e.g. cut lows and highs so the delay doesn't clutter)" },
                { "Vybrať zvuk (WAV, MP3…)", "Choose a sound (WAV, MP3…)" }, { "O priečinok vyššie", "Up one folder" },
                { "Pridať / odobrať priečinok z obľúbených", "Add / remove folder from favourites" }, { "Hľadať v priečinku…", "Search in folder…" },
                { "aj podpriečinky", "include subfolders" }, { "Prehrať pri výbere", "Play on select" }, { "Do vrstvy A", "To layer A" }, { "Do vrstvy B", "To layer B" },
                { "MIESTA", "PLACES" }, { "Hudba", "Music" }, { "Plocha", "Desktop" }, { "Dokumenty", "Documents" }, { "Stiahnuté", "Downloads" }, { "OBĽÚBENÉ", "FAVOURITES" },
                { " – vrstva A", " – layer A" }, { " – vrstva B", " – layer B" }, { " položiek", " items" }, { " (zobrazené sú len prvé)", " (only the first are shown)" },
                { "  ·  ↑↓ výber, Enter načíta, medzerník prehrá, Backspace = vyššie", "  ·  ↑↓ select, Enter loads, space plays, Backspace = up" },
                { "MIDI ovládač", "MIDI controller" }, { "MIDI learn – potom pohni gombíkom na klaviatúre", "MIDI learn – then move a knob on your keyboard" },
                { "Zrušiť priradenie", "Remove assignment" }, { "Pôvodná hodnota", "Default value" },
                { "Modulácia", "Modulation" }, { "Typ modulácie", "Modulation type" }, { "Spätná väzba", "Feedback" }, { "Sp. väzba", "Feedback" },
                { "Chorus = širší zvuk, Ensemble = sláčikový ensemble, Flanger a Phaser = „lietadlo“, Tremolo = kolísanie hlasitosti", "Chorus = wider sound, Ensemble = string ensemble, Flanger and Phaser = \"jet\" sweep, Tremolo = volume wobble" },
                { "Oneskorenie dozvuku – oddelí čistý zvuk od dozvuku, znie zreteľnejšie", "Reverb pre-delay – separates the dry sound from the reverb, sounds clearer" },
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
                { "Sínus|Trojuholník|Píla|Obdĺžnik|Šum|Pluck|FM|Sampel|Roj|Wavetable", "Sine|Triangle|Saw|Square|Noise|Pluck|FM|Sample|Swarm|Wavetable" },
                // wavetable a displej hlasov
                { "Wavetable", "Wavetable" }, { "WT", "WT" }, { "Tabuľka", "Table" }, { "Pozícia", "Position" }, { "PWM", "PWM" }, { "Harmonický", "Harmonic" },
                { "Vokál", "Vowel" }, { "Digitál", "Digital" }, { "Sklo", "Glass" }, { "Spev", "Choir" }, { "ALIKVOTY", "PARTIALS" }, { "1 HLAS", "1 VOICE" },
                { "WT Vokál pad", "WT Vowel Pad" }, { "WT Digitál lead", "WT Digital Lead" }, { "WT Analog bas", "WT Analog Bass" },
                { "Tabuľka tvarov vlny: Analóg (píla–obdĺžnik–pulz), PWM, Harmonický (rezonančný prechod), Vokál (A-E-I-O-U), Digitál, Organ, Sklo, Spev",
                  "Wave-shape table: Analog (saw–square–pulse), PWM, Harmonic (resonant sweep), Vowel (A-E-I-O-U), Digital, Organ, Glass, Choir" },
                { "Cieľ LFO: Výška = vibrato, Filter, Hlasitosť = tremolo, Panoráma, Farba = hýbe gombíkom Farba (šírka pulzu, FM index, Focus, pozícia WT)",
                  "LFO target: Pitch = vibrato, Filter, Volume = tremolo, Pan, Color = moves the Color knob (pulse width, FM index, Focus, WT position)" },
                { "Pozícia vo wavetable – plynulý prechod medzi 64 tvarmi vlny. Hýbe ňou aj Obálka (filtra) a LFO s cieľom Farba.",
                  "Wavetable position – smooth morph through 64 wave shapes. Also moved by Env amt (filter envelope) and an LFO with target Color." },
                { "Displej hlasov: zvislé čiary = hlasy unisona (vodorovne rozladenie v centoch, výška = hlasitosť hlasu). Pri WT je v pozadí aktuálny tvar vlny, pri Roji alikvoty.",
                  "Voice display: vertical lines = unison voices (horizontal = detune in cents, height = voice level). WT shows the current wave shape behind, Swarm shows its partials." },
                { "Roj", "Swarm" }, { "Focus", "Focus" }, { "Rozptyl", "Scatter" }, { "Bohatosť", "Richness" },
                { "Sínus", "Sine" }, { "Trojuholník", "Triangle" }, { "Píla", "Saw" }, { "Obdĺžnik", "Square" }, { "Šum", "Noise" }, { "Sampel", "Sample" },
                { "Dolná", "Low-pass" }, { "Pásmová", "Band-pass" }, { "Horná", "High-pass" }, { "Telo", "Body" },
                { "Troj.", "Tri" }, { "Obdĺž.", "Square" }, { "Náhod.", "Random" }, { "Výška", "Pitch" }, { "Hlasit.", "Volume" }, { "Panor.", "Pan" },
                { "Vlastný WAV", "Own WAV" }, { "Voľne", "Free" }, { "1/8 bodka", "1/8 dotted" }, { "1/8 triola", "1/8 triplet" },
                { "Hore", "Up" }, { "Dole", "Down" }, { "Hore/Dole", "Up/Down" }, { "Ako hrám", "As played" }, { "Náhodne", "Random" }, { "Akord", "Chord" },
                // Inspiracia (zachytenie toho, co si prave zahral)
                { "Zachytí, čo si práve zahral (aj bez nahrávania) – v Korgus Sequenceri to vloží do stopy, inde ponúkne uloženie MIDI alebo WAV",
                  "Captures what you just played (even without recording) – in Korgus Sequencer it goes into the track, elsewhere you can save it as MIDI or WAV" },
                // menu zvukov podla tabulky nastrojov
                { "Prírodné", "Acoustic" }, { "Syntetické", "Synthetic" }, { "Brnkacie", "Plucked" }, { "Drevené dychy", "Woodwinds" }, { "Plechové dychy", "Brass" },
                { "Mechové", "Reeds & organ" }, { "Melodické bicie", "Mallets" }, { "Bicie", "Drums" }, { "Atmosféry", "Textures" }, { "Elektrické", "Electric" },
                { "Import zvuku – pridať zvuk zo súboru .knova (napr. z iného počítača)", "Import sound – add a sound from a .knova file (e.g. from another computer)" },
                { "Pridať – nový priečinok na zvuky, nový zvuk, zvuk z WAV", "Add – new sound folder, new sound, sound from WAV" }, { "Pomoc k zoznamu zvukov", "Help for the sound list" },
                // okno Inspiracie
                { "Inšpirácia: zachytí, čo si zahral (aj bez nahrávania). Klik otvorí celé MIDI – vyberieš úsek a vložíš ho do stopy alebo uložíš ako MIDI / WAV. ↺ v rohu = vymazať a počúvať od ďalšieho tónu",
                  "Inspiration: captures what you played (even without recording). Click opens the whole MIDI – pick a section and insert it into the track or save it as MIDI / WAV. ↺ in the corner = clear and listen from the next note" },
                { "Viola", "Viola" }, { "Pikola", "Piccolo" }, { "Banjo", "Banjo" }, { "Tympany", "Timpani" }, { "Bicia súprava", "Drum kit" },
                { "Ručné bubny", "Hand drums" }, { "Perkusie", "Percussion" }, { " (sťahujem ", " (downloading " },
                { " (nedá sa stiahnuť – skontroluj internet)", " (can't download – check the internet)" },
                { "Sťahujem nástroj… ", "Downloading instrument… " }, { "Nástroj sa nedá stiahnuť – skontroluj internet", "The instrument can't be downloaded – check the internet" },
                { "Akordeón", "Accordion" }, { "Gajdy", "Bagpipe" }, { "Klasická gitara", "Classical guitar" },
                { "Vymazať", "Clear" }, { "Vymazať zachytené hranie – Inšpirácia bude počúvať od ďalšieho tónu", "Clear what was captured – Inspiration listens from the next note" },
                { "Vymazané – počúvam od ďalšieho tónu", "Cleared – listening from the next note" },
                { "Zavrieť", "Close" }, { "Celé", "All" }, { "Ukázať celé hranie", "Show everything you played" }, { "Posledná fráza", "Last phrase" },
                { "Vybrať to, čo si hral naposledy (po poslednej dlhšej pauze)", "Select what you played last (after the last longer pause)" },
                { "Vybrať všetko", "Select all" }, { "Vybrať celé hranie", "Select everything you played" }, { "Oddialiť", "Zoom out" }, { "Priblížiť", "Zoom in" },
                { "Vložiť do stopy", "Insert into track" }, { "Vybraný úsek sa vloží do stopy v Korgus Sequenceri", "The selected section is inserted into the track in Korgus Sequencer" },
                { "Uložiť MIDI…", "Save MIDI…" }, { "Uložiť vybraný úsek ako MIDI súbor", "Save the selected section as a MIDI file" },
                { "Uložiť WAV…", "Save WAV…" }, { "Uložiť vybraný úsek ako zvuk WAV (s aktuálnym zvukom)", "Save the selected section as WAV audio (with the current sound)" },
                { "Vybraté: ", "Selected: " }, { " nôt · ", " notes · " }, { "Nič nie je vybraté", "Nothing selected" },
                { "Všetko, čo si zahral za posledných 10 minút · ťahaním v notách vyberieš úsek, vlajočky L a R ho upravia · koliesko = zoom · Shift + koliesko alebo ťahanie pravítka = posun · dvojklik = celé",
                  "Everything you played in the last 10 minutes · drag in the notes to select, the L and R flags adjust it · wheel = zoom · Shift + wheel or dragging the ruler = scroll · double-click = all" },
                { "Zatiaľ si nič nezahral. Zahraj niečo na klaviatúre – objaví sa to tu.", "You haven't played anything yet. Play something on the keyboard – it will show up here." },
                { "✦ Inšpirácia", "✦ Inspiration" },
                { "Zatiaľ si nič nezahral", "You haven't played anything yet" }, { "Pridané do stopy", "Added to the track" },
                { "Uložiť ako MIDI…", "Save as MIDI…" }, { "Uložiť ako WAV…", "Save as WAV…" }, { "Nauč sa zvuk", "Learn sound" }, { "Citlivosť kláves – ako silno treba udrieť", "Key sensitivity – how hard you need to play" }, { "Jemná – aj slabší úder znie silno", "Soft – even a light touch sounds strong" }, { "Normálna", "Normal" }, { "Tvrdá – na silný zvuk treba udrieť silno", "Hard – you need to play hard for a strong sound" }, { "Ukážka: pri výbere zvuku zo zoznamu krátko zahrá", "Preview: plays briefly when you pick a sound from the list" }, { "Efekty zap / vyp: delay, dozvuk, EQ, modulácia, drive, Tube a arpeggiátor. Len na počúvanie – zvuk sa nemení.", "Effects on / off: delay, reverb, EQ, modulation, drive, Tube and arpeggiator. Just for listening – the sound is not changed." }, { "Inspiria sa pokúsi napodobniť nástroj z inej stopy v Korgus Sequenceri.", "Inspiria tries to imitate an instrument from another track in Korgus Sequencer." }, { "Nauč sa zvuk funguje v programe Korgus Sequencer: Inspiria tam napodobní nástroj z inej stopy.", "Learn sound works in Korgus Sequencer: there Inspiria imitates an instrument from another track." }, { "Vypnúť = jednoduchá verzia so šiestimi gombíkmi", "Turn off = simple version with six knobs" },
                { "Ukladám…", "Saving…" }, { "Uložené: ", "Saved: " }, { "Nepodarilo sa uložiť", "Couldn't save" },
                { "Ukladám zvuk…", "Saving sound…" },
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
            const auto nd = docs.getChildFile ("Korgus Inspiria"), oi = docs.getChildFile ("Korgus Inspire"), od = docs.getChildFile ("Korgus Nova");
            if (! nd.exists() && oi.isDirectory()) oi.moveFileTo (nd);
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
