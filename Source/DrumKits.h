#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Kniznica zvukov bicich: syntetizovane supravy pre 10 riadkov krokoveho sekvencera
// (Kick, Snare, Clap, HH zatvoreny, HH otvoreny, Tom nizky, Tom vysoky, Rim, Cinel, Cowbell)
namespace DrumKits
{
    constexpr int numKits = 9, numLanes = 10;
    juce::String kitName (int kit);          // "Pop", "House", "Drum & Bass", "Afro", "Latino", "Hlboká", "Teplá", "Svetlá", "Krátka a tvrdá"
    bool isSampleKit (int kit);              // supravy zo samplov (Resources/kits), ostatne su syntetizovane
    juce::String laneName (int kit, int lane);
    // vytvori zvuk (mono, spicka 0.9); false pri zlom indexe
    bool make (int kit, int lane, double sr, juce::AudioBuffer<float>& out);
}
