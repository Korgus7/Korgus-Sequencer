#include "UiCommon.h"

// ---------------------------------------------------------------- jazyk (slovencina / anglictina)
#include <unordered_map>
int& appLang() { static int l = 0; return l; }

juce::String skTranslate (const char* utf8)
{
    if (appLang() == 1 && utf8 != nullptr)
    {
        static const std::unordered_map<std::string, const char*> t = {
        { "Porovnať", "Compare" },
        { "Krátkodobá (LUFS)", "Short-term (LUFS)" },
        { "Celková (LUFS)", "Integrated (LUFS)" },
        { "Špička (dBTP)", "True peak (dBTP)" },
        { "Redukcia v pásmach (dB)", "Band gain reduction (dB)" },
        { "Cieľ: ", "Target: " },
        { "Dvojklik: vynulovať merače", "Double-click: reset meters" },
        { "Vyber stopu so spevom ako kľúč (sidechain)", "Pick the vocal track as the key (sidechain)" },
        { "Pravý klik na stopu so spevom → Kľúčová stopa (sidechain). Efekt dáš na stopu s nástrojom (nosič).", "Right-click the vocal track → Key track (sidechain). Put this effect on the instrument track (the carrier)." },
        { "Zahraj akordy (MIDI) alebo zvoľ Robot", "Play chords (MIDI) or choose Robot" },
        { "Noty: MIDI na tejto stope, klávesy (vybraná stopa) alebo akordová stopa piesne. Efekt patrí na stopu so spevom.", "Notes: MIDI on this track, keyboard (selected track) or the song's chord track. Put this effect on the vocal track." },
        { "Robot: nosič sleduje výšku hlasu", "Robot: the carrier follows the pitch of the voice" },
        { "Robot trojhlas: hlas + terca + kvinta", "Robot harmony: voice + third + fifth" },
        { "Tónina piesne: ", "Song key: " },
        { "Nosič: zvuk tejto stopy, modulátor: kľúčová stopa (spev)", "Carrier: this track's sound, modulator: the key track (vocals)" },
        { "Hlas zo stopy označenej ako kľúčová; jej fader ho neovplyvní (kľúč ide pred hlasitosťou).", "The voice comes from the track marked as key; its fader does not affect it (the key is taken before the volume)." },
        { "Interný syntezátor hrá", "The internal synth is playing" },
        { "neznáma (voľná výška)", "unknown (free pitch)" },
        { " moll", " minor" },
        { " dur", " major" },
        { " a zisťujem tempo…", " and detecting the tempo…" },
        { "Aktualizáciu sa nepodarilo stiahnuť. Skontroluj internet.", "Couldn't download the update. Check your internet connection." },
        { "Aktualizáciu sa nepodarilo uložiť.", "Couldn't save the update." },
        { "Inštalátor sa nepodarilo spustiť.", "Couldn't start the installer." },
        { "Windows (Inteligentná kontrola aplikácií) nepustil novú verziu. Ostáva ti doterajšia verzia.", "Windows (Smart App Control) blocked the new version. You keep the current version." },
        { "Novú verziu sa nepodarilo spustiť. Spusti Korgus Sequencer znova.", "Couldn't start the new version. Start Korgus Sequencer again." },
        { "Učenie zvuku už prebieha.", "Sound learning is already running." },
        { "Stopa nemá nástroj.", "The track has no instrument." },
        { "Nástroj sa nepodarilo pripraviť.", "Couldn't prepare the instrument." },
        { " krokov", " steps" },
        { " stôp)", " tracks)" },
        { " vzoriek = ", " samples = " },
        { ") – len prvý raz…", ") – first time only…" },
        { "1/4|1/8|1/8 triola|1/8 bodka|1/16|1/16 triola|1/32", "1/4|1/8|1/8 triplet|1/8 dotted|1/16|1/16 triplet|1/32" },
        { "2 hlasy|4 hlasy|6 hlasov", "2 voices|4 voices|6 voices" },
        { "Analyzujem nahrávku", "Analysing the recording" },
        { "Analyzujem…", "Analysing…" },
        { "Balím projekt", "Packing the project" },
        { "Balím zvuk ", "Packing sound " },
        { "Basa… (3/3)", "Bass… (3/3)" },
        { "Basy", "Low" },
        { "Basy Hz", "Low Hz" },
        { "Bicie… (2/3)", "Drums… (2/3)" },
        { "Bounce stopy do audia", "Bounce track to audio" },
        { "Celý zvuk", "Whole sound" },
        { "Chýba ONNX Runtime: ", "ONNX Runtime missing: " },
        { "Chýba kľúč OpenRouter.", "OpenRouter key missing." },
        { "Chýbajúce pluginy", "Missing plugins" },
        { "Citlivosť", "Velocity" },
        { "Dokumenty", "Documents" },
        { "Dolaďujem hlasitosť…", "Fine-tuning the loudness…" },
        { "Dolaďujem spev", "Tuning vocals" },
        { "Dolaďujem spev…", "Tuning vocals…" },
        { "Dorovnávam hlasitosť…", "Matching loudness…" },
        { "Dozvuk", "Reverb" },
        { "Dočasný súbor sa nedá prečítať.", "Can't read the temporary file." },
        { "Dvojhlas|Harmonizér|Oboje", "Doubler|Harmonizer|Both" },
        { "Dĺžka tónu", "Note length" },
        { "Dĺžka vzoru: ", "Pattern length: " },
        { "Hotové vzory", "Ready patterns" }, { "Kocka", "Dice" }, { "Náhodný vzor – sila, pauzy a oktávy", "Random pattern – velocity, rests and octaves" },
        { "Basa v oktávach", "Octave bass" }, { "Hore a dole", "Up and down" }, { "Akordy na osminy", "Eighth-note chords" }, { "Zimmer pulz", "Zimmer pulse" },
        { "Export MP3 – ", "Export MP3 – " },
        { "Export stôp", "Export tracks" },
        { "Export stôp – vyber priečinok", "Export tracks – choose a folder" },
        { "Export videa (MP4)", "Export video (MP4)" },
        { "Export videa – ", "Export video – " },
        { "Farba", "Color" },
        { "Frekvencia", "Frequency" },
        { "Hlasitosť", "Volume" },
        { "Hlasy", "Voices" },
        { "Hore|Dole|Hore-dole|Dole-hore|Náhodne|Ako hráš|Akord", "Up|Down|Up-down|Down-up|Random|As played|Chord" },
        { "Hotovo", "Done" },
        { "Hrať", "Play" },
        { "Hudba", "Music" },
        { "Hĺbka", "Depth" },
        { "Hľadám doby, tempo, akordy a údery…", "Finding beats, tempo, chords and hits…" },
        { "Hľadám pluginy vedľa programu…", "Looking for plugins next to the program…" },
        { "Import audio (WAV, MP3)", "Import audio (WAV, MP3)" },
        { "Klip je príliš krátky (aspoň 4 sekundy).", "The clip is too short (at least 4 seconds)." },
        { "Klip je príliš krátky.", "The clip is too short." },
        { "Korgus Sequencer – zbalený projekt\r\n\r\nOtvor ho v programe: Súbor → Otvoriť (súbor .kpack).\r\n\r\n", "Korgus Sequencer – packed project\r\n\r\nOpen it in the program: File → Open (.kpack file).\r\n\r\n" },
        { "Kroky", "Steps" },
        { "Kým držím klávesu", "While key is held" },
        { "Kľúčová nota", "Root note" },
        { "Ladenie", "Tuning" },
        { "MP3: enkóder sa nespustil.", "MP3: the encoder didn't start." },
        { "MP3: nepodporované nastavenie.", "MP3: unsupported setting." },
        { "Mastering asistent", "Mastering assistant" },
        { "Max. stíšenie", "Max. reduction" },
        { "Meriam hlasitosť mixu", "Measuring the mix loudness" },
        { "Mäkký nábeh|Klasika|Neskorý nástup|Hlboká pumpa|Rýchly nábeh|Opačne|Mäkký pokles|Vrchol|Dlhé podržanie|Plynulý nábeh|Tvrdá pumpa|Brána|Plató|Svah|Údolie|Dvojitá", "Soft rise|Classic|Late start|Deep pump|Fast rise|Reverse|Soft fall|Peak|Long hold|Smooth rise|Hard pump|Gate|Plateau|Slope|Valley|Double" },
        { "Na inom počítači ho otvor cez Súbor → Otvoriť.", "On the other computer open it via File → Open." },
        { "Nahrávam pieseň…", "Recording the song…" },
        { "Najlepšia odozva je s typom zariadenia ASIO a malým bufferom (64 – 256). Ak zvuk praská, zväčši buffer. MIDI klávesy zaškrtni v zozname MIDI vstupov.", "The best response is with the ASIO device type and a small buffer (64 – 256). If the sound crackles, increase the buffer. Tick MIDI keyboards in the list of MIDI inputs." },
        { "Nastavenia zvuku", "Audio settings" },
        { "Načítavam ", "Loading " },
        { "Načítavam nástroj: ", "Loading instrument: " },
        { "Načítavam zvuk ", "Loading sound " },
        { "Načítavam zvukové súbory…", "Loading audio files…" },
        { "Načítať zvuk…", "Load sound…" },
        { "Nedá sa zapísať dočasný súbor.", "Can't write the temporary file." },
        { "Nedá sa zapísať: ", "Can't write: " },
        { "Nepodarilo sa spojiť s OpenRouter. Skontroluj internet.", "Couldn't connect to OpenRouter. Check the internet." },
        { "Nábeh", "Attack" },
        { "ONNX Runtime má nesprávnu verziu.", "ONNX Runtime has the wrong version." },
        { "ONNX Runtime sa nedá načítať: ", "Can't load ONNX Runtime: " },
        { "ONNX Runtime: chýba OrtGetApiBase.", "ONNX Runtime: OrtGetApiBase missing." },
        { "Obrázok na pozadie videa", "Background picture for the video" },
        { "Odšumujem nahrávku", "Denoising the recording" },
        { "Odšumujem nahrávku…", "Denoising the recording…" },
        { "Oktávy", "Octaves" },
        { "Oneskorenie", "Delay" },
        { "Opakovanie", "Feedback" },
        { "Otvoriť", "Open" },
        { "Otvoriť pieseň", "Open song" },
        { "Otváram poslednú pieseň…", "Opening the last song…" },
        { "Otváram zvukovú kartu…", "Opening the sound card…" },
        { "Oddeľujem spev a hudbu", "Separating vocals and music" },
        { "Oddeľujem spev a hudbu… (pesnička 3 min ≈ 1 – 3 min)", "Separating vocals and music… (3 min song ≈ 1 – 3 min)" },
        { "Rozkladám pesničku na stopy", "Splitting the song into tracks" },
        { "Spev… (1/3)", "Vocals… (1/3)" },
        { "Pieseň je tichá – nie je čo masterovať.", "The song is silent – nothing to master." },
        { "Plocha", "Desktop" },
        { "Plugin sa nedá načítať", "Can't load the plugin" },
        { "Pluginy a priečinky…", "Plugins and folders…" },
        { "Pluginy – zoznam a priečinky", "Plugins – list and folders" },
        { "Zaškrtni pluginy, ktoré chceš používať. Nezaškrtnuté program pri spustení nenačíta a nie sú v ponuke. Nové pluginy nájdeš na karte Hľadať a priečinky.", "Tick the plugins you want to use. Unticked ones are not loaded at startup and are not offered. Find new plugins on the Search and folders tab." },
        { "Zapnúť všetky", "Enable all" }, { "Vypnúť všetky", "Disable all" },
        { "nástroj", "instrument" }, { "efekt", "effect" },
        { "Moje pluginy", "My plugins" }, { "Štandard −14 LUFS", "Standard −14 LUFS" }, { "Jemne −16 LUFS", "Gentle −16 LUFS" }, { "Hlasno −9 LUFS", "Loud −9 LUFS" }, { "Rádio / TV −23 LUFS", "Radio / TV −23 LUFS" }, { "Slučky", "Loops" }, { "Zakázané", "Blocked" }, { "Správa pluginov", "Plugin manager" }, { "Žiadny plugin nie je zakázaný.", "No plugin is blocked." }, { "Povoliť", "Allow" }, { "Pluginy, ktoré spôsobili pád programu. Program ich nenačíta. Klikni na riadok – plugin sa znova povolí.", "Plugins that crashed the program. They are not loaded. Click a row to allow the plugin again." }, { "Hľadať plugin podľa názvu alebo výrobcu…", "Search plugins by name or maker…" }, { "Hľadať a priečinky", "Search and folders" },
        { "Podržať", "Hold" },
        { "Pomer", "Ratio" },
        { "Potrebné VST pluginy (musia byť nainštalované aj na druhom počítači):\r\n- ", "Required VST plugins (must also be installed on the other computer):\r\n- " },
        { "Počúvam mix…", "Listening to the mix…" },
        { "Počúvať sykavky", "Listen to sibilants" },
        { "Prah", "Threshold" },
        { "Pretiahni sem zvuk (wav, mp3…) alebo klikni na „Načítať zvuk…“", "Drag a sound here (wav, mp3…) or click \"Load sound…\"" },
        { "Projekt je zbalený", "The project is packed" },
        { "Projekt nepotrebuje žiadne ďalšie VST pluginy.\r\n", "The project needs no other VST plugins.\r\n" },
        { "Práve prebieha iný export.", "Another export is running." },
        { "Referenčná pesnička", "Reference song" },
        { "Režim", "Mode" },
        { "Riadenie", "Control" },
        { "Rozladenie", "Detune" },
        { "Rytmus", "Rhythm" },
        { "Rýchlosť", "Rate" },
        { "SIDECHAIN – riadi kľúčová stopa", "SIDECHAIN – driven by the key track" },
        { "Sila", "Velocity" },
        { "Smer", "Direction" },
        { "Stiahnuté", "Downloads" },
        { "Bicie Korgus", "Korgus drums" },
        { "Kroky", "Steps" },
        { "PULSEDIP – KROKY  (ťahaj myšou, pravý klik = zap/vyp)", "PULSEDIP – STEPS  (drag with the mouse, right-click = on/off)" },
        { "Stlačenie", "Reduction" },
        { "Stopa ", "Track " },
        { "Stopa sa nenašla.", "Track not found." },
        { "Stredy", "Mid" },
        { "Stredy Hz", "Mid Hz" },
        { "Strop", "Ceiling" },
        { "Stíšenie sykaviek", "Sibilance reduction" },
        { "Synchro", "Sync" },
        { "Súbor nie je platný zbalený projekt.", "The file isn't a valid packed project." },
        { "Súbor sa nedá prečítať ako MIDI.", "Can't read the file as MIDI." },
        { "Súbor sa nedá prečítať.", "Can't read the file." },
        { "Súbor sa nedá zapísať.", "Can't write the file." },
        { "Sťahovanie sa prerušilo.", "The download was interrupted." },
        { "Sťahovanie zlyhalo (", "Download failed (" },
        { "Sťahujem ONNX Runtime (65 MB) – len prvý raz…", "Downloading ONNX Runtime (65 MB) – first time only…" },
        { "Sťahujem model ", "Downloading model " },
        { "Tento súbor sa nedá prečítať.", "This file can't be read." },
        { "Tento typ MIDI súboru (SMPTE čas) nie je podporovaný.", "This type of MIDI file (SMPTE time) isn't supported." },
        { "Tieto pluginy sa na tomto počítači nenašli – nainštaluj ich a nechaj program vyhľadať pluginy:", "These plugins weren't found on this computer – install them and let the program search for plugins:" },
        { "Tlmenie", "Damping" },
        { "Predozvuk", "Pre-delay" },
        { "Tvar", "Shape" },
        { "Typ:        ", "Type:       " },
        { "Ukladám MP3…", "Saving MP3…" },
        { "Uložiť", "Save" },
        { "Uvoľnenie", "Release" },
        { "V nahrávke sa nenašiel zreteľný rytmus.", "No clear rhythm found in the recording." },
        { "Veľkosť", "Size" },
        { "Dĺžka", "Decay" },
        { "Veľkosť pluginu", "Plugin size" },
        { "Vlastný", "Own" },
        { "Voľne|1/4|1/8 bodka|1/8|1/8 triola|1/16", "Free|1/4|1/8 dotted|1/8|1/8 triplet|1/16" },
        { "Vyber zvuk pre sampler", "Choose a sound for the sampler" },
        { "Vytváram video (môže to chvíľu trvať)…", "Creating the video (may take a while)…" },
        { "Vzor", "Pattern" },
        { "Vzor vypnutý – hrá každý krok, sila z hry", "Pattern off – plays every step, velocity as played" },
        { "Vzorkovanie: ", "Sample rate: " },
        { "Výsledok sa nepodarilo uložiť.", "Couldn't save the result." },
        { "Výstup", "Output" },
        { "Výstupná latencia: ", "Output latency: " },
        { "Výšky", "High" },
        { "Výšky Hz", "High Hz" },
        { "Z hry|Zo vzoru|Kombinácia", "Played|From pattern|Combined" },
        { "Z hry", "Played" },
        { "Zapisujem súbor…", "Writing the file…" },
        { "Zapnuté", "On" },
        { "Zariadenie: ", "Device: " },
        { "Zbaliť projekt", "Pack project" },
        { "Zisk", "Gain" },
        { "Zrušené.", "Cancelled." },
        { "Zvuk je dlhší ako 2 minúty.", "The sound is longer than 2 minutes." },
        { "Zvuk klipu sa nenašiel.", "The clip's audio wasn't found." },
        { "Zvuk pre bicie (WAV, MP3)", "Sound for drums (WAV, MP3)" },
        { "Zvuková karta: ", "Sound card: " },
        { "Zvukové súbory: ", "Audio files: " },
        { "Zápis sa nepodaril.", "Writing failed." },
        { "Záťaž CPU:  ", "CPU load:   " },
        { "efekty stopy", "track effects" },
        { "hlasitosť ", "volume " },
        { "klik = krok zap/vyp · ťahanie = sila · dolný riadok = oktáva", "click = step on/off · drag = velocity · bottom row = octave" },
        { "piesen", "song" },
        { "Úsek so šumom medzi lokátormi je príliš krátky (treba aspoň 0,1 s v klipe).", "The noise section between the locators is too short (needs at least 0.1 s in the clip)." },
        { "Čas", "Time" },
        { "Šírka", "Width" },
        { "Šírka Q", "Width Q" },
        { "Žiadne zvukové zariadenie nie je otvorené.", "No audio device is open." },
        { "žiadny|−12 oktáva dole|−7 kvinta dole|−5 kvarta dole|+3 malá terca|+4 veľká terca|+5 kvarta|+7 kvinta|+12 oktáva", "none|−12 octave down|−7 fifth down|−5 fourth down|+3 minor third|+4 major third|+5 fourth|+7 fifth|+12 octave" },
        { "−12 oktáva dole|−7 kvinta dole|−5 kvarta dole|+3 malá terca|+4 veľká terca|+5 kvarta|+7 kvinta|+12 oktáva", "−12 octave down|−7 fifth down|−5 fourth down|+3 minor third|+4 major third|+5 fourth|+7 fifth|+12 octave" },
        { "Predvoľba…", "Preset…" },
        { "Hotové nastavenia – vyber a dolaď gombíkmi", "Ready-made settings – pick one and fine-tune with the knobs" },
        { "A/B: dve nastavenia na porovnanie – prepni a počúvaj rozdiel", "A/B: two settings to compare – switch and listen to the difference" },
        { "Vypnúť efekt – zvuk ide bez úprav (na porovnanie)", "Turn the effect off – the sound passes unchanged (to compare)" },
        { "Vypnuté", "Off" },
        { "Vstup", "Input" },
        { "Odreže hlboké basy pod touto frekvenciou (hučanie, dunenie). V grafe: ľavý bod.", "Cuts deep bass below this frequency (hum, rumble). In the graph: the left point." },
        { "Zosilní alebo stíši basy. V grafe ťahaj bod B hore / dole.", "Boosts or cuts the bass. In the graph drag point B up / down." },
        { "Odkiaľ začínajú basy. V grafe ťahaj bod B do strany.", "Where the bass starts. In the graph drag point B sideways." },
        { "Zosilní alebo stíši stredy (hlas, gitara). V grafe ťahaj bod S.", "Boosts or cuts the mids (voice, guitar). In the graph drag point S." },
        { "Ktoré stredy sa menia. V grafe ťahaj bod S do strany.", "Which mids change. In the graph drag point S sideways." },
        { "Šírka pásma stredov: malé číslo = široké, veľké = úzke. V grafe koliesko myši na bode S.", "Width of the mid band: small number = wide, large = narrow. In the graph use the mouse wheel on point S." },
        { "Zosilní alebo stíši výšky (jas, vzduch). V grafe ťahaj bod V.", "Boosts or cuts the highs (brightness, air). In the graph drag point V." },
        { "Odkiaľ začínajú výšky. V grafe ťahaj bod V do strany.", "Where the highs start. In the graph drag point V sideways." },
        { "Celková hlasitosť po ekvalizácii.", "Overall volume after the EQ." },
        { "Ťahaj body: LC = odrezanie basov, B = basy, S = stredy (koliesko = šírka), V = výšky · dvojklik na bod = nula", "Drag the points: LC = low cut, B = bass, S = mids (wheel = width), V = highs · double-click a point = zero" },
        { "Odrezanie basov", "Low cut" },
        { "vyp.", "off" },
        { "Od akej hlasitosti začne kompresor stláčať. V grafe ťahaj zlatý bod do strany.", "The level where the compressor starts working. In the graph drag the gold point sideways." },
        { "Ako silno stláča nad prahom (4:1 = o 4 dB viac na vstupe dá len 1 dB na výstupe). V grafe ťahaj čiaru vpravo hore / dole.", "How hard it compresses above the threshold (4:1 = 4 dB more in gives only 1 dB more out). In the graph drag the line on the right up / down." },
        { "Ako rýchlo začne stláčať. Pomalší nábeh nechá prejsť úder (bicie znejú údernejšie).", "How fast it starts compressing. A slower attack lets the hit through (drums sound punchier)." },
        { "Ako rýchlo sa po stlačení vráti späť.", "How fast it lets go after compressing." },
        { "Zosilní výsledok, aby bol rovnako hlasný ako predtým.", "Boosts the result so it's as loud as before." },
        { "Pomer stlačeného a pôvodného zvuku (paralelná kompresia).", "Balance of the compressed and the original sound (parallel compression)." },
        { "Sidechain: stláča podľa inej stopy (napr. basa uhne kopáku).", "Sidechain: compresses following another track (e.g. the bass ducks under the kick)." },
        { "Krivka: ťahaj zlatý bod (prah) do strany, čiaru vpravo hore / dole (pomer). Bodka ukazuje aktuálnu hlasitosť.", "Curve: drag the gold point (threshold) sideways, the line on the right up / down (ratio). The dot shows the current level." },
        { "vstup", "input" },
        { "prah", "threshold" },
        { "Veľkosť priestoru – od malej izby po veľkú halu (rozostup prvých odrazov).", "Size of the space – from a small room to a big hall (spacing of the first reflections)." },
        { "Ako dlho dozvuk doznieva. V grafe ťahaj koniec chvosta.", "How long the reverb lasts. In the graph drag the end of the tail." },
        { "Tlmenie výšok: viac = tmavší, mäkší dozvuk (ako v miestnosti so závesmi).", "High damping: more = darker, softer reverb (like a room with curtains)." },
        { "Medzera pred dozvukom – spev ostane zreteľný a dozvuk príde až za ním. V grafe ťahaj začiatok chvosta.", "Gap before the reverb – vocals stay clear and the reverb comes after them. In the graph drag the start of the tail." },
        { "Šírka dozvuku v stereu.", "Stereo width of the reverb." },
        { "Koľko dozvuku pridať k pôvodnému zvuku.", "How much reverb to add to the original sound." },
        { "Ťahaj začiatok chvosta (predozvuk) alebo jeho koniec (dĺžka). Tlačidlá dole = hotové miestnosti.", "Drag the start of the tail (pre-delay) or its end (length). Buttons below = ready-made rooms." },
        { "predozvuk", "pre-delay" },
        { "dĺžka", "length" },
        { "Čas medzi opakovaniami (pri Synchro podľa tempa piesne). V grafe ťahaj prvú čiarku do strany.", "Time between repeats (with Sync it follows the song tempo). In the graph drag the first line sideways." },
        { "Koľkokrát sa ozvena opakuje. V grafe ťahaj prvú čiarku hore / dole.", "How many times the echo repeats. In the graph drag the first line up / down." },
        { "Farba ozveny: menej = tmavšia, každé ďalšie opakovanie tmavne.", "Echo colour: less = darker, each repeat gets darker." },
        { "Koľko ozveny pridať k pôvodnému zvuku.", "How much echo to add to the original sound." },
        { "Ping-pong: ozvena skáče striedavo vľavo a vpravo.", "Ping-pong: the echo bounces left and right." },
        { "Ťahaj prvú zlatú čiarku: do strany = čas, hore / dole = počet opakovaní.", "Drag the first gold line: sideways = time, up / down = number of repeats." },
        { "Čas nastavíš voľne v milisekundách", "Set the time freely in milliseconds" },
        { "Čas podľa tempa piesne", "Time follows the song tempo" },
        { "Zosilní zvuk pred limiterom – výsledok bude hlasnejší, špičky sa zrežú na strop.", "Boosts the sound before the limiter – the result is louder, peaks are held at the ceiling." },
        { "Najvyššia povolená úroveň. −0,3 až −1 dB je bezpečné pre MP3 a streamovanie.", "Highest allowed level. −0.3 to −1 dB is safe for MP3 and streaming." },
        { "Ako rýchlo sa limiter uvoľní. Kratší = hlasnejšie, ale môže skresľovať.", "How fast the limiter recovers. Shorter = louder, but may distort." },
        { "Od akej frekvencie sú sykavky (s, š, c). V grafe ťahaj zlatú čiaru alebo stlač Nájdi sykavky.", "Frequency where sibilants (s, sh, ts) start. In the graph drag the gold line or press Find sibilants." },
        { "Od akej hlasitosti sykaviek sa začne stišovať. V grafe vpravo ťahaj zlatú značku.", "Sibilance level where reduction starts. In the graph drag the gold mark on the right." },
        { "Najviac o koľko sa sykavky stíšia.", "Maximum sibilance reduction." },
        { "Počuješ len to, čo sa stišuje – pomôže nájsť správnu frekvenciu.", "You hear only what gets reduced – helps find the right frequency." },
        { "Ťahaj zlatú čiaru v spektre (frekvencia) a zlatú značku vpravo (prah). Nájdi sykavky = nastaví sa samo.", "Drag the gold line in the spectrum (frequency) and the gold mark on the right (threshold). Find sibilants = sets it up for you." },
        { "Nájdi sykavky", "Find sibilants" },
        { "Pusti spev a stlač – program si 4 sekundy počúva a nastaví frekvenciu aj prah sám.", "Play the vocals and press – the program listens for 4 seconds and sets the frequency and threshold itself." },
        { "sykavky od", "sibilants from" },
        { "sykavky", "sibilants" },
        { "Počúvam spev…", "Listening to the vocals…" },
        { "Nič som nepočul – spusti prehrávanie so spevom a skús znova.", "I heard nothing – start playback with vocals and try again." },
        { "Hotovo: sykavky od", "Done: sibilants from" },
        { "Bez úprav", "Flat" },
        { "Spev – čistý", "Vocals – clear" },
        { "Spev – teplý", "Vocals – warm" },
        { "Akustická gitara", "Acoustic guitar" },
        { "Basa – viac úderu", "Bass – more punch" },
        { "Bicie – úder", "Drums – punch" },
        { "Telefón", "Telephone" },
        { "Menej hučania", "Less rumble" },
        { "Spev – jemne", "Vocals – gentle" },
        { "Spev – vyrovnaný", "Vocals – even" },
        { "Bicie – paralelne", "Drums – parallel" },
        { "Basa – vyrovnaná", "Bass – even" },
        { "Mix – lepidlo", "Mix – glue" },
        { "Pumpa (sidechain)", "Pump (sidechain)" },
        { "Spev – jemná ozvena", "Vocals – soft echo" },
        { "Slapback (rokenrol)", "Slapback (rock'n'roll)" },
        { "Ping-pong", "Ping-pong" },
        { "Dlhá do priestoru", "Long and spacious" },
        { "Dub (tmavá)", "Dub (dark)" },
        { "Bezpečný strop", "Safe ceiling" },
        { "Hlasnejšie", "Louder" },
        { "Veľmi hlasno", "Very loud" },
        { "Mužský hlas", "Male voice" },
        { "Ženský hlas", "Female voice" },
        { "Jemne", "Gentle" },
        { "Silno", "Strong" },
        { "Izba", "Room" },
        { "Štúdio", "Studio" },
        { "Sála", "Hall" },
        { "Kostol", "Church" },
        { "Plech", "Plate" },
        { "Spev", "Vocals" },
        { "Krivka = ako sa mení hlasitosť počas jednej doby. Ťahaj krivku do strany = posun. Vlastný = guličky ťaháš myšou. Pod krivkou je živý zvuk (sivý = pred, zlatý = po).", "Curve = how the volume changes during one beat. Drag the curve sideways = offset. Custom = drag the dots with the mouse. Behind the curve is the live sound (grey = before, gold = after)." },
        { "Vlastný tvar: guličky na krivke ťaháš myšou", "Custom shape: drag the dots on the curve with the mouse" },
        { "Ako často pumpuje: 1/4 = na každú dobu (ako kopák), 1/8 = dvakrát rýchlejšie, T = trioly.", "How often it pumps: 1/4 = every beat (like a kick), 1/8 = twice as fast, T = triplets." },
        { "Ako hlboko stíši: 100 % = úplne, 30 % = jemné dýchanie.", "How deep it ducks: 100 % = fully, 30 % = gentle breathing." },
        { "Hlasitosť po efekte.", "Volume after the effect." },
        { "Tempo = podľa tempa piesne, Sidechain = podľa inej stopy (kopák), Kroky = 16 krokov kreslených myšou.", "Tempo = follows the song tempo, Sidechain = follows another track (kick), Steps = 16 steps drawn with the mouse." },
        { "Zapne / vypne pumpovanie.", "Turns pumping on / off." },
        { "Posunie krivku v čase – napr. pumpa začne chvíľu po kopáku. V grafe ťahaj krivku do strany.", "Shifts the curve in time – e.g. the pump starts a moment after the kick. In the graph drag the curve sideways." },
        { "Stišuje len basy (pod 180 Hz) – spev, hi-haty a výšky ostanú bez pumpovania.", "Ducks only the bass (below 180 Hz) – vocals, hi-hats and highs stay unpumped." },
        { "Posun", "Offset" },
        { "Len basy", "Bass only" },
        { "posun", "offset" },
        { "len basy", "bass only" },
        { "Klasická pumpa", "Classic pump" },
        { "Jemné dýchanie", "Gentle breathing" },
        { "Len basy – pumpa", "Bass only – pump" },
        { "Trance brána 1/16", "Trance gate 1/16" },
        { "Rýchle osminy", "Fast eighths" },
        { "Pomalá vlna 1/2", "Slow wave 1/2" },
        { "Neskorá pumpa", "Late pump" },
        { "Sidechain z kopáku", "Sidechain from the kick" },
        { "Rádio / TV", "Radio / TV" },
        { "Merač hlasitosti: radar = hlasitosť za posledné 2 minúty, zelená = v cieli, žltá = hlasnejšie, modrá = tichšie.", "Loudness meter: radar = loudness over the last 2 minutes, green = on target, yellow = louder, blue = quieter." },
        { "Cieľová hlasitosť (LUFS) pre túto službu", "Target loudness (LUFS) for this service" },
        { "Reset", "Reset" },
        { "Vynuluje meranie (začne odznova)", "Clears the measurement (starts again)" },
        { "Celková hlasitosť (Integrated)", "Program loudness (Integrated)" },
        { "Najvyššia špička (True Peak)", "True-peak max" },
        { "Najhlasnejšie miesto", "Loudness max" },
        { "Rozsah hlasitosti (LRA)", "Loudness range (LRA)" },
        { "Špička – hlasitosť (PLR)", "Peak to loudness (PLR)" },
        { "Korelácia (fáza)", "Correlation (phase)" },
        { "Meranie", "Measured" },
        { "Ľavý", "Left" },
        { "Pravý", "Right" },
        { "Cieľ", "Target" },
        { "Spúšťam…", "Starting…" },
        { "Zvuková karta: ", "Sound card: " },
        };
        auto it = t.find (utf8);
        if (it != t.end()) return juce::String::fromUTF8 (it->second);
    }
    return juce::String::fromUTF8 (utf8);
}

juce::String sk (const char* utf8) { return skTranslate (utf8); }

namespace Col
{
    const juce::Colour steel   (0xff2d4362);
    const juce::Colour panel   (0xfff4f5f7);   // svetle telo okien (jednotny vzhlad)
    const juce::Colour panelLo (0xffeceef1);
    const juce::Colour button  (0xffffffff);
    const juce::Colour border  (0xffc9cfd6);
    const juce::Colour text    (0xff1f2833);
    const juce::Colour lcdBg   (0xff18212c);
    const juce::Colour lcdFg   (0xffc3d39a);
}

//==============================================================================
KorgusLookAndFeel::KorgusLookAndFeel()
{
    setColourScheme ({ Col::panel, Col::panel, Col::panelLo, Col::border, Col::text,
                       Col::button, juce::Colours::white, Col::steel, Col::text });
    setColour (juce::TextButton::buttonColourId, Col::button);
    setColour (juce::TextButton::buttonOnColourId, Col::steel);
    setColour (juce::TextButton::textColourOffId, Col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, Col::border);
    setColour (juce::ComboBox::textColourId, Col::text);
    setColour (juce::ComboBox::arrowColourId, Col::text);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::white);
    setColour (juce::PopupMenu::textColourId, Col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffe1e8f1));
    setColour (juce::PopupMenu::highlightedTextColourId, Col::text);
    setColour (juce::Label::textColourId, Col::text);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::white);
    setColour (juce::ListBox::outlineColourId, Col::border);
    setColour (juce::ToggleButton::textColourId, Col::text);
    setColour (juce::ToggleButton::tickColourId, Col::steel);
    setColour (juce::ToggleButton::tickDisabledColourId, Col::border);
    setColour (juce::Slider::thumbColourId, Col::steel);
    setColour (juce::Slider::trackColourId, Col::border);
    setColour (juce::Slider::backgroundColourId, Col::panelLo);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xffaeb7c2));
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId, Col::border);
    setColour (juce::TextEditor::focusedOutlineColourId, Col::steel);
    setColour (juce::TextEditor::textColourId, Col::text);
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0xffd3dce8));
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabTextColourId, juce::Colour (0xff4a5563));
    setColour (juce::TabbedButtonBar::frontTextColourId, Col::text);
    setColour (juce::AlertWindow::backgroundColourId, Col::panel);
    setColour (juce::AlertWindow::textColourId, Col::text);
    setColour (juce::AlertWindow::outlineColourId, Col::border);
    setColour (juce::ProgressBar::foregroundColourId, Col::steel);
    setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xffdde1e6));
}

void KorgusLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bg;
    if (down)      c = c.darker (0.10f);
    else if (over) c = b.getToggleState() ? c.brighter (0.12f) : c.darker (0.05f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (b.getToggleState() ? Col::steel.darker (0.3f) : Col::border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void KorgusLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double progress, const juce::String& text)
{
    juce::Rectangle<float> r (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (juce::Colour (0xffdde1e6));
    g.fillRoundedRectangle (r, 3.0f);
    if (progress >= 0.0 && progress <= 1.0)
    {
        g.setColour (Col::steel);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (0.0f, r.getWidth() * (float) progress)), 3.0f);
    }
    else   // neurcity priebeh: posuvajuci sa pruh
    {
        const float t = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1400.0, 1.0), bw = r.getWidth() * 0.3f;
        g.setColour (Col::steel);
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + (r.getWidth() + bw) * t - bw, r.getY(), bw, r.getHeight()).getIntersection (r), 3.0f);
    }
    if (text.isNotEmpty())
    {
        // text citatelny na oboch castiach pruhu: biely s tmavym obrysom
        g.setFont (juce::FontOptions ((float) juce::jmin (15, h - 4), juce::Font::bold));
        const auto tr = r.toNearestInt();
        g.setColour (juce::Colour (0xcc111821));
        for (auto d : { juce::Point<int> (1, 1), juce::Point<int> (-1, 1), juce::Point<int> (1, -1), juce::Point<int> (-1, -1) })
            g.drawText (text, tr.translated (d.x, d.y), juce::Justification::centred, false);
        g.setColour (juce::Colours::white);
        g.drawText (text, tr, juce::Justification::centred, false);
    }
}

void KorgusLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colour (0xffe1e4e8));
    g.fillRect (0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void KorgusLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    const auto r = b.getActiveArea();
    const bool front = b.isFrontTab();
    g.setColour (front ? Col::text : (over ? Col::text : juce::Colour (0xff4a5563)));
    g.setFont (juce::FontOptions (14.5f, front ? juce::Font::bold : juce::Font::plain));
    g.drawText (b.getButtonText().trim(), r, juce::Justification::centred, false);
    if (front) { g.setColour (Col::steel); g.fillRect (r.getX() + 8, r.getBottom() - 2, r.getWidth() - 16, 2); }
}

void KorgusLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& a, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    g.fillAll (Col::panel);
    g.setColour (juce::Colour (0xffc3c9d1));
    g.drawRect (a.getLocalBounds(), 1);
    layout.draw (g, textArea.toFloat());
}

void KorgusLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> r (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path p;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (p);
}

namespace
{
    // lista s velkostou nad pluginom + samotny plugin (pripadne v posuvnom okne)
    class PluginHolder : public juce::Component, private juce::ComponentListener
    {
    public:
        static constexpr int barH = 26;

        PluginHolder (juce::AudioProcessorEditor* e, float scale, std::function<void (float)> onScale)
            : ed (e), onScaleFn (std::move (onScale))
        {
            label.setText (sk ("Veľkosť pluginu"), juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, juce::Colour (0xffe4e9ef));
            label.setFont (juce::FontOptions (13.0f));
            addAndMakeVisible (label);
            const int pct[] = { 75, 100, 125, 150, 175, 200, 250 };
            for (int k = 0; k < 7; ++k) size.addItem (juce::String (pct[k]) + " %", pct[k]);
            size.setSelectedId (juce::roundToInt (scale * 100.0f) / 25 * 25 > 0 ? juce::roundToInt (scale * 100.0f) : 100, juce::dontSendNotification);
            if (size.getSelectedId() == 0) size.setSelectedId (100, juce::dontSendNotification);
            size.onChange = [this] { applyScale ((float) size.getSelectedId() / 100.0f, true); };
            addAndMakeVisible (size);

            if (ed != nullptr)
            {
                if (std::abs (scale - 1.0f) > 0.01f) ed->setScaleFactor (scale);
                fitToScreen();
                box.addAndMakeVisible (ed);
                addAndMakeVisible (box);
                ed->addComponentListener (this);
                updateSize();
            }
            else setSize (300, barH + 60);
        }
        ~PluginHolder() override
        {
            if (ed != nullptr) ed->removeComponentListener (this);
            vp = nullptr;
            delete ed;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff2a3442));
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto bar = r.removeFromTop (barH).reduced (6, 3);
            label.setBounds (bar.removeFromLeft (110));
            size.setBounds (bar.removeFromLeft (90));
            if (vp != nullptr) vp->setBounds (r);
            else box.setTopLeftPosition (r.getPosition());
        }

    private:
        static juce::Rectangle<int> screenArea()
        {
            auto a = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced (20);
            a.removeFromTop (40 + barH);
            return a;
        }
        // velkost editora po zvacseni (setScaleFactor meni transformaciu, nie getWidth)
        juce::Rectangle<int> edSize() const { return ed->getLocalBounds().transformedBy (ed->getTransform()).withZeroOrigin(); }
        bool tooBig() const
        {
            const auto a = screenArea();
            const auto e = edSize();
            return e.getWidth() > a.getWidth() || e.getHeight() > a.getHeight();
        }
        // plugin s menitelnou velkostou zmensime na obrazovku
        void fitToScreen()
        {
            if (ed == nullptr || ! ed->isResizable() || ! tooBig()) return;
            const auto a = screenArea();
            ed->setSize (juce::jmin (ed->getWidth(), a.getWidth()), juce::jmin (ed->getHeight(), a.getHeight()));
        }
        void applyScale (float s, bool save)
        {
            if (ed == nullptr) return;
            ed->setScaleFactor (s);
            fitToScreen();
            updateSize();
            if (auto* w = findParentComponentOfClass<juce::ResizableWindow>()) w->setContentComponentSize (getWidth(), getHeight());
            if (save && onScaleFn) onScaleFn (s);
        }
        void updateSize()
        {
            if (ed == nullptr) return;
            // editor so zmenenou mierkou je v obale velkom ako jeho skutocny obraz
            const auto es = edSize();
            ed->setTopLeftPosition (0, 0);
            box.setSize (es.getWidth(), es.getHeight());
            const bool big = tooBig();
            if (big && vp == nullptr)
            {
                removeChildComponent (&box);
                vp = std::make_unique<juce::Viewport>();
                vp->setViewedComponent (&box, false);
                vp->setScrollBarsShown (true, true);
                addAndMakeVisible (*vp);
            }
            else if (! big && vp != nullptr)
            {
                vp->setViewedComponent (nullptr, false);
                vp = nullptr;
                addAndMakeVisible (box);
            }
            const auto a = screenArea();
            const auto e = edSize();
            const int w = juce::jmax (300, vp != nullptr ? juce::jmin (e.getWidth() + 16, a.getWidth()) : e.getWidth());
            const int h = (vp != nullptr ? juce::jmin (e.getHeight() + 16, a.getHeight()) : e.getHeight()) + barH;
            setSize (w, h);
            resized();
        }
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (wasResized) updateSize();
        }

        juce::AudioProcessorEditor* ed = nullptr;
        juce::Component box;
        std::unique_ptr<juce::Viewport> vp;
        juce::Label label;
        juce::ComboBox size;
        std::function<void (float)> onScaleFn;
    };
}

PluginWindow::PluginWindow (juce::AudioProcessor& p, const juce::String& title, std::function<void()> onClose,
                            float scale, std::function<void (float)> onScale)
    : DocumentWindow (title, Col::steel, DocumentWindow::closeButton | DocumentWindow::minimiseButton), onCloseFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    auto* holder = new PluginHolder (p.createEditorIfNeeded(), scale, std::move (onScale));
    setContentOwned (holder, true);
    setResizable (false, false);
    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced (20);
    area.removeFromTop (40);   // miesto pre titulok okna, aby sa dalo vzdy zavriet
    setTopLeftPosition (area.getX() + juce::jmax (0, (area.getWidth() - getWidth()) / 2),
                        area.getY() + juce::jmax (0, (area.getHeight() - getHeight()) / 3));
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
}

namespace
{
    // zoznam cudzich pluginov so zaskrtavanim - zaskrtnute sa pouzivaju, ostatne program nenacita
    struct PluginChooser : juce::Component, juce::ListBoxModel, juce::ChangeListener
    {
        PluginChooser (juce::KnownPluginList& k, std::function<bool (const juce::PluginDescription&)> own,
                       std::function<bool (const juce::PluginDescription&)> on,
                       std::function<void (const juce::StringArray&, bool)> set)
            : kl (k), isOwn (std::move (own)), isOn (std::move (on)), setOn (std::move (set))
        {
            info.setText (sk ("Zaškrtni pluginy, ktoré chceš používať. Nezaškrtnuté program pri spustení nenačíta a nie sú v ponuke. "
                              "Nové pluginy nájdeš na karte Hľadať a priečinky."), juce::dontSendNotification);
            info.setFont (juce::FontOptions (14.0f)); info.setColour (juce::Label::textColourId, Col::text);
            info.setJustificationType (juce::Justification::topLeft);
            allOn.setButtonText (sk ("Zapnúť všetky")); allOff.setButtonText (sk ("Vypnúť všetky"));
            allOn.onClick = [this] { setAll (true); }; allOff.onClick = [this] { setAll (false); };
            search.setTextToShowWhenEmpty (sk ("Hľadať plugin podľa názvu alebo výrobcu…"), Col::text.withAlpha (0.5f));
            search.setFont (juce::FontOptions (14.0f));
            search.onTextChange = [this] { refresh(); };
            list.setModel (this); list.setRowHeight (26);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colours::white);
            for (auto* c : std::initializer_list<juce::Component*> { &info, &search, &allOn, &allOff, &list }) addAndMakeVisible (c);
            kl.addChangeListener (this);
            refresh();
        }
        ~PluginChooser() override { kl.removeChangeListener (this); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void refresh()
        {
            types.clear();
            const auto q = search.getText().trim();
            for (const auto& d : kl.getTypes())
                if (! isOwn (d) && (q.isEmpty() || d.name.containsIgnoreCase (q) || d.manufacturerName.containsIgnoreCase (q))) types.add (d);
            std::sort (types.begin(), types.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
            {
                if (a.isInstrument != b.isInstrument) return a.isInstrument;
                return a.name.compareIgnoreCase (b.name) < 0;
            });
            list.updateContent(); list.repaint();
        }
        void setAll (bool on)
        {
            juce::StringArray u;
            for (const auto& d : types) u.add (d.createIdentifierString());
            setOn (u, on); list.repaint();
        }
        int getNumRows() override { return types.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            if (! juce::isPositiveAndBelow (row, types.size())) return;
            const auto& d = types.getReference (row);
            const bool on = isOn (d);
            auto box = juce::Rectangle<float> (8.0f, (float) h * 0.5f - 8.0f, 16.0f, 16.0f);
            g.setColour (juce::Colour (0xff9aa4b0)); g.drawRoundedRectangle (box, 3.0f, 1.2f);
            if (on)
            {
                g.setColour (Col::steel); g.fillRoundedRectangle (box.reduced (3.0f), 2.0f);
            }
            g.setColour (on ? Col::text : Col::text.withAlpha (0.5f));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (d.name, 34, 0, w / 2 - 34, h, juce::Justification::centredLeft, true);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (d.manufacturerName, w / 2, 0, w / 4, h, juce::Justification::centredLeft, true);
            g.drawText ((d.isInstrument ? sk ("nástroj") : sk ("efekt")) + "  " + d.pluginFormatName,
                        w * 3 / 4, 0, w / 4 - 8, h, juce::Justification::centredLeft, true);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (! juce::isPositiveAndBelow (row, types.size())) return;
            const auto& d = types.getReference (row);
            setOn ({ d.createIdentifierString() }, ! isOn (d));
            list.repaintRow (row);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            info.setBounds (r.removeFromTop (44));
            search.setBounds (r.removeFromTop (28)); r.removeFromTop (6);
            auto b = r.removeFromBottom (30); r.removeFromBottom (8);
            allOn.setBounds (b.removeFromLeft (140)); b.removeFromLeft (8); allOff.setBounds (b.removeFromLeft (140));
            list.setBounds (r);
        }
        void paint (juce::Graphics& g) override { g.fillAll (Col::panel); }

        juce::KnownPluginList& kl;
        std::function<bool (const juce::PluginDescription&)> isOwn, isOn;
        std::function<void (const juce::StringArray&, bool)> setOn;
        juce::Array<juce::PluginDescription> types;
        juce::Label info; juce::TextEditor search; juce::TextButton allOn, allOff; juce::ListBox list;
    };
}

namespace
{
    // zakazane pluginy (spadli a program ich odlozil) - klik na riadok = povolit
    struct BlockedList : juce::Component, juce::ListBoxModel
    {
        BlockedList (std::function<juce::StringArray()> g, std::function<void (const juce::String&)> u) : get (std::move (g)), unblock (std::move (u))
        {
            info.setText (sk ("Pluginy, ktoré spôsobili pád programu. Program ich nenačíta. Klikni na riadok – plugin sa znova povolí."), juce::dontSendNotification);
            info.setFont (juce::FontOptions (14.0f)); info.setColour (juce::Label::textColourId, Col::text);
            info.setJustificationType (juce::Justification::topLeft);
            list.setModel (this); list.setRowHeight (30);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colours::white);
            addAndMakeVisible (info); addAndMakeVisible (list);
            refresh();
        }
        void visibilityChanged() override { if (isVisible()) refresh(); }
        void refresh() { items = get ? get() : juce::StringArray(); list.updateContent(); list.repaint(); }
        int getNumRows() override { return juce::jmax (1, items.size()); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            g.setFont (juce::FontOptions (14.0f));
            if (items.isEmpty())
            {
                if (row == 0) { g.setColour (Col::text.withAlpha (0.55f)); g.drawText (sk ("Žiadny plugin nie je zakázaný."), 10, 0, w - 20, h, juce::Justification::centredLeft); }
                return;
            }
            if (! juce::isPositiveAndBelow (row, items.size())) return;
            const auto name = items[row].fromFirstOccurrenceOf ("\t", false, false);
            g.setColour (Col::text);
            g.drawText (name.isNotEmpty() ? name : items[row], 10, 0, w - 120, h, juce::Justification::centredLeft, true);
            g.setColour (Col::steel);
            g.drawText (sk ("Povoliť"), w - 110, 0, 100, h, juce::Justification::centredRight);
            g.setColour (juce::Colour (0xffeef0f3)); g.fillRect (0, h - 1, w, 1);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (! juce::isPositiveAndBelow (row, items.size())) return;
            if (unblock) unblock (items[row].upToFirstOccurrenceOf ("\t", false, false));
            refresh();
        }
        void resized() override { auto r = getLocalBounds().reduced (10); info.setBounds (r.removeFromTop (44)); list.setBounds (r); }
        void paint (juce::Graphics& g) override { g.fillAll (Col::panel); }
        std::function<juce::StringArray()> get;
        std::function<void (const juce::String&)> unblock;
        juce::StringArray items;
        juce::Label info; juce::ListBox list;
    };
}

PluginManagerWindow::PluginManagerWindow (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& kl, const juce::File& pedal,
                                          juce::PropertiesFile* props, std::function<void()> onClose,
                                          std::function<bool (const juce::PluginDescription&)> isOwn,
                                          std::function<bool (const juce::PluginDescription&)> isOn,
                                          std::function<void (const juce::StringArray&, bool)> setOn,
                                          std::function<juce::StringArray()> getBlocked,
                                          std::function<void (const juce::String&)> unblock)
    : DocumentWindow (sk ("Správa pluginov"), Col::steel, DocumentWindow::closeButton),
      onCloseFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    auto* tabs = new juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop);
    tabs->addTab (sk ("Moje pluginy"), Col::panel, new PluginChooser (kl, std::move (isOwn), std::move (isOn), std::move (setOn)), true);
    tabs->addTab (sk ("Hľadať a priečinky"), Col::panel, new juce::PluginListComponent (fm, kl, pedal, props, true), true);
    tabs->addTab (sk ("Zakázané"), Col::panel, new BlockedList (std::move (getBlocked), std::move (unblock)), true);
    tabs->setTabBarDepth (40);
    tabs->setOutline (0);
    tabs->setSize (820, 560);
    setContentOwned (tabs, true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

