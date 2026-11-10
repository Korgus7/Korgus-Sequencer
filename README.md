# Korgus Sequencer

Jednoduchý hudobný program pre Windows v štýle Cubase 1.0 – aranžmán, krokový sekvencer,
key editor, audio stopy – postavený na C++ a JUCE, s ASIO a VST3 pluginmi.

## Stav

- **Krok 1 – zvukový motor:** ASIO / Windows Audio, MIDI vstupy, VST3 nástroje, latencia.
- **Krok 2 – rozhranie:** aranžmán, krokový sekvencer, key editor (HTML v okne programu, WebView2);
  každá stopa hrá cez vlastný VST3 nástroj, presné časovanie v C++ motore, metronóm, odpočet, cyklus.

## Preklad

Windows `.exe` prekladá automaticky GitHub Actions (`.github/workflows/build-windows.yml`)
pri každom pushnutí do `main`. Hotový súbor je v artefakte `KorgusSequencer-windows`.

Lokálne (Visual Studio 2022 + CMake):

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DASIO_SDK_DIR=C:\cesta\k\asiosdk
    cmake --build build --config Release

## Licencie

Používa JUCE 8, VST3 SDK (MIT) a ASIO SDK (GPLv3). Program je na súkromné použitie;
pri šírení treba dodržať podmienky týchto licencií.
