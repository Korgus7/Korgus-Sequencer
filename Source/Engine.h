#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <array>
#include "AudioStore.h"

// Jedna nota na casovej osi (v krokoch = 1/16)
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
    AudioBufPtr buf;
};

struct TrackData
{
    bool isAudio = false;
    bool monitor = false;       // odposluch vstupu (audio stopa)
    int inA = 0, inB = -1;      // vstupne kanaly zvukovky (inB = -1 -> mono)
    std::vector<ClipEv> clips;
    int slot = -1;              // index slotu s pluginom v Engine
    int channel = 1;            // MIDI kanal pre VST nastroj
    float gainL = 1, gainR = 1; // hlasitost + panorama
    bool audible = true;        // nie je stlmena / je solo
    std::vector<NoteEv> notes;  // zoradene podla start
};

struct SongData
{
    double tempo = 120, endStep = 512, locL = 0, locR = 64;
    float masterGain = 1;
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
    int blockSize = 512;
    double countSamples = 0, beatSamples = 0;
    int countBeatsLeft = 0;

    // nahravanie
    std::atomic<bool> recArmed { false }, recActive { false }, recStopped { false };
    std::atomic<double> recStartStep { 0 };
    std::vector<std::unique_ptr<Rec>> recs;
    std::atomic<int> numRecs { 0 };
    juce::MidiBuffer fxMidi;
    juce::SpinLock filterLock;
    juce::String midiFilter;
    const float* const* curIn = nullptr;
    int curNumIn = 0;
    float* const* curOut = nullptr;
    int curNumOut = 0;
    std::atomic<bool> offline { false };
    int masterSlot = -1;   // slot "__master" s efektmi celej piesne

    // metronom
    int clickLeft = 0, clickLen = 0, clickDelay = 0;
    double clickPhase = 0, clickInc = 0;
    double lastBeatStep = -1;
};
