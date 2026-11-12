#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <array>
#include "AudioStore.h"

// Jedna nota na casovej osi (v krokoch = 1/16)
struct CcEv
{
    double start = 0;
    int ctrl = 1;      // 0..127 = CC, 128 = pitch bend
    int value = 0;     // CC 0..127, pitch bend 0..16383
};

struct NoteEv
{
    double start = 0, end = 0;
    int pitch = 60;
    float vel = 0.8f;
};

// Zvukovy klip na audio stope
struct ClipEv
{
    double start = 0;     // krok zaciatku
    double off = 0;       // posun v subore (s)
    double dur = 0;       // dlzka (s)
    double fi = 0, fo = 0; // nabeh / dobeh (s)
    float gain = 1;
    AudioBufPtr buf;
};

// Uder bicich zo samplu
struct HitEv
{
    double start = 0;
    AudioBufPtr buf;
    float gain = 1;
    int lane = 0;
};

// Krivka automatizacie: type 0 = hlasitost, 1 = panorama, 2 = parameter pluginu (which 0 = nastroj, 1..4 = efekt)
struct AutoLane
{
    int type = 0, which = 0, param = 0;
    std::vector<std::pair<double, float>> pts;   // krok, hodnota 0..1 (zoradene)
    float valueAt (double step) const
    {
        if (pts.empty()) return 0;
        if (step <= pts.front().first) return pts.front().second;
        if (step >= pts.back().first) return pts.back().second;
        auto it = std::upper_bound (pts.begin(), pts.end(), step, [] (double v, const std::pair<double, float>& p) { return v < p.first; });
        const auto& b = *it; const auto& a = *(it - 1);
        const double t = (step - a.first) / juce::jmax (1.0e-9, b.first - a.first);
        return (float) (a.second + (b.second - a.second) * t);
    }
};

struct TrackData
{
    float vol = 0.8f, pan = 0;  // zakladna hlasitost a panorama (pre automatizaciu)
    std::vector<AutoLane> autos;
    std::vector<HitEv> hits;    // zoradene podla start
    bool isAudio = false;
    bool monitor = false;       // odposluch vstupu (audio stopa)
    int inA = 0, inB = -1;      // vstupne kanaly zvukovky (inB = -1 -> mono)
    std::vector<ClipEv> clips;
    int slot = -1;              // index slotu s pluginom v Engine
    int channel = 1;            // MIDI kanal pre VST nastroj
    float gainL = 1, gainR = 1; // hlasitost + panorama
    bool audible = true;        // nie je stlmena / je solo
    float send[2] { 0, 0 };     // posielanie do FX 1 / FX 2 (za hlasitostou)
    bool isKey = false;         // klucova stopa pre sidechain
    std::vector<NoteEv> notes;  // zoradene podla start
    std::vector<CcEv> ccs;      // MIDI CC a pitch bend, zoradene podla start
};

struct SongData
{
    double tempo = 120, endStep = 512, locL = 0, locR = 64;
    float masterGain = 1;
    float auxGain[2] { 1, 1 };  // navrat FX 1 / FX 2
    bool loopOn = false, click = false;
    std::vector<TrackData> tracks;
};

// Zvukovy motor: prehrava piesen cez VST nastroje stop, ziva hra z MIDI, metronom a odpocet
class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback
{
public:
    Engine();
    ~Engine() override;

    //== volane z hlavneho vlakna
    int  getOrCreateSlot (const juce::String& trackId);
    void setSong (std::unique_ptr<SongData>);
    void play (double fromStep, int countInBars);
    void stop();
    void setPosition (double step);
    void liveNote (int slot, int pitch, float vel, bool on);
    void setSelectedSlot (int slot)                 { selectedSlot = slot; }
    void setTrackPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance>);
    juce::AudioPluginInstance* getPlugin (int slot);
    static constexpr int numFx = 4;
    void setTrackFx (int slot, int index, std::unique_ptr<juce::AudioPluginInstance>);
    juce::AudioPluginInstance* getFx (int slot, int index);
    void setMidiFilter (const juce::String& deviceId);
    void triggerSample (int slot, AudioBufPtr buf, float gain, int lane);   // ziva hra / nahlad samplu
    juce::String getSlotTrackId (int slot);
    int  getNumSlots();

    // nahravanie zo vstupu zvukovky (aj viac stop naraz)
    struct RecSpec { int a = 0, b = -1; };
    void   startRecording (const std::vector<RecSpec>&);
    void   stopRecording()           { recArmed = false; }
    bool   hasRecStarted() const     { return recActive.load(); }
    double getRecStartStep() const   { return recStartStep.load(); }
    void   drainRecording (int index, std::vector<float>& left, std::vector<float>& right);

    // export do WAV (zvukovka musi byt odpojena)
    bool renderOffline (const SongData&, double endStep, double tailSec, juce::AudioFormatWriter&,
                        const std::function<bool (double)>& progress);
    double getSampleRate() const     { return sampleRate; }
    int    getBlockSize() const      { return blockSize; }

    double getPosition() const       { return publicPos.load(); }
    bool   isPlaying() const         { return state.load() == playing; }
    bool   isCounting() const        { return state.load() == counting; }
    int    getCountBeat() const      { return countBeat.load(); }
    int    getCountTotal() const     { return countTotal.load(); }

    // MIDI z klaves - zavolane na hlavnom vlakne (pre nahravanie a krokovy vstup)
    std::function<void (int pitch, float vel, bool on, double pos)> onMidiIn;

    //== AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

private:
    enum State { stopped, counting, playing };

    struct Slot
    {
        juce::String trackId;
        int channel = 1;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::array<std::unique_ptr<juce::AudioPluginInstance>, numFx> fx;
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf;
        std::vector<std::pair<int, double>> active; // pitch, koniec
        float curL = -1, curR = -1;                        // posledne zosilnenie (plynuly prechod)
        std::vector<std::pair<int, float>> lastParams;     // posledne nastavene parametre z automatizacie
        struct Voice { AudioBufPtr buf; juce::int64 pos = 0; float gain = 1; int lane = 0; int fade = -1; int fadeLen = 64; };
        std::vector<Voice> voices;
        void addVoice (AudioBufPtr b, juce::int64 pos, float gain, int lane);
        void renderVoices (juce::AudioBuffer<float>& dst, int n);
        void prepare (double sr, int bs);
        int  numChans() const;
    };
    struct Rec
    {
        explicit Rec (int cap) : fifo (cap), l ((size_t) cap), r ((size_t) cap) {}
        int a = 0, b = -1;
        juce::AbstractFifo fifo;
        std::vector<float> l, r;
    };

    struct Live { int slot; juce::MidiMessage msg; };
    struct Cmd  { int type; double a; int b; };   // 1=play 2=stop 3=setPos

    void process (const float* const* in, int numIn, float* const* out, int numOut, int n, const SongData* forced);
    void renderSegment (const SongData&, double p0, double p1, int off, int len, double samplesPerStep);
    void mixClips (const TrackData&, juce::AudioBuffer<float>& dst, double p0, int off, int len, double samplesPerStep);
    void forEachPlugin (const std::function<void (juce::AudioPluginInstance&)>&);
    void captureInput (int off, int len, double p0);
    void allNotesOff (int sampleOffset, bool hard);
    void addClick (int offset, bool accent);
    void renderClicks (float* const* out, int numOut, int n);

    juce::CriticalSection slotLock;
    std::vector<std::unique_ptr<Slot>> slots;

    std::atomic<SongData*> song { nullptr };
    std::vector<std::unique_ptr<SongData>> songHistory;   // stare verzie sa mazu neskor (nie v audio vlakne)

    juce::AbstractFifo liveFifo { 512 };
    std::vector<Live> liveBuf { 512 };
    juce::SpinLock cmdLock;
    std::vector<Cmd> cmds;

    std::atomic<int> state { stopped }, selectedSlot { -1 }, countBeat { 0 }, countTotal { 0 };
    std::atomic<double> publicPos { 0 };
    double pos = 0, sampleRate = 44100, startAfterCount = 0;

    // poloha a tempo pre pluginy (synchronizacia s tempom)
    struct HostHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i;
            i.setBpm (bpm);
            i.setTimeSignature (TimeSignature { 4, 4 });
            i.setPpqPosition (ppq);
            i.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            i.setTimeInSamples (samples);
            i.setTimeInSeconds (sr > 0 ? (double) samples / sr : 0.0);
            i.setIsPlaying (playing);
            return i;
        }
        double bpm = 120, ppq = 0, sr = 44100;
        juce::int64 samples = 0;
        bool playing = false;
    } head;
    int blockSize = 512;
    double countSamples = 0, beatSamples = 0;
    int countBeatsLeft = 0;

    // nahravanie
    std::atomic<bool> recArmed { false }, recActive { false }, recStopped { false };
    std::atomic<double> recStartStep { 0 };
    std::vector<std::unique_ptr<Rec>> recs;
    std::atomic<int> numRecs { 0 };
    juce::MidiBuffer fxMidi;
    juce::SpinLock filterLock, trigLock;
    struct Trig { int slot; AudioBufPtr buf; float gain; int lane; };
    std::vector<Trig> trigs;
    juce::String midiFilter;
    const float* const* curIn = nullptr;
    int curNumIn = 0;
    float* const* curOut = nullptr;
    int curNumOut = 0;
    std::atomic<bool> offline { false };
    int masterSlot = -1;
    int auxSlot[2] { -1, -1 };
    juce::AudioBuffer<float> keyBuf { 2, 512 };   // sidechain kluc
    std::vector<int> order;                       // poradie spracovania stop  // sloty "__aux1", "__aux2" (spolocne efekty)
public:
    std::atomic<bool> bypassMaster { false };   // export stop: bez efektov a hlasitosti mastra
private:   // slot "__master" s efektmi celej piesne

    // metronom
    int clickLeft = 0, clickLen = 0, clickDelay = 0;
    double clickPhase = 0, clickInc = 0;
    double lastBeatStep = -1;
};
