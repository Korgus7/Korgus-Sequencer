#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <vector>

// Druhy zvukovy vstup: popri hlavnej karte (napr. Scarlett cez ASIO) nahrava z inej karty (napr. AKAI MPC) cez Windows Audio.
// Karty maju vlastne hodiny - vstup ide cez kruhovy zasobnik a jemne sa prevzorkuje tak, aby zasobnik drzal stalu
// naplnenost (rozdiel hodin sa vyrovna bez cvakania). Pre program vyzera ako dalsie dva vstupne kanaly hlavnej karty.
class SecondInput : private juce::AudioIODeviceCallback
{
public:
    SecondInput() = default;
    ~SecondInput() override { close(); }

    // zoznam vstupnych zariadeni Windows Audio
    juce::StringArray deviceNames();
    // otvori zariadenie (na hlavnom vlakne); preferredRate = vzorkovanie hlavnej karty
    bool open (const juce::String& name, double preferredRate, juce::String& error);
    void close();
    bool isOpen() const { return active.load(); }
    juce::String deviceName() const { return devName; }
    juce::StringArray channelNames() const;   // napr. "MPC Sample: 1", "MPC Sample: 2"

    //== volane z audio vlakna hlavnej karty
    // naplni l/r n vzorkami pri vzorkovani hlavnej karty; false = vstup nie je otvoreny (ticho)
    bool pull (float* l, float* r, int n, double mainRate);
    // oneskorenie vstupu vo vzorkach hlavnej karty (na posun nahravky)
    int latencySamples() const { return latMain.load(); }

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    static constexpr int kFrames = 1 << 17;          // ~2,7 s pri 48 kHz
    static constexpr juce::int64 kMask = kFrames - 1;
    std::vector<float> ring = std::vector<float> ((size_t) kFrames * 2, 0.0f);
    std::atomic<juce::int64> wpos { 0 };
    std::atomic<bool> active { false };
    std::atomic<double> srcRate { 48000.0 };
    std::atomic<int> srcBlock { 480 }, srcLat { 0 }, latMain { 0 };

    // stav citania (len audio vlakno hlavnej karty)
    double rpos = 0, fillAvg = 0, lastMainRate = 0;
    int lastBlock = 0;
    bool started = false;

    juce::SpinLock lock;   // open/close vs. citanie
    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String devName;
    int numCh = 0;
};
