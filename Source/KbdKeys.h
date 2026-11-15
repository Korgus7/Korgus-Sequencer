#pragma once
// Pomocne funkcie pre hranie z klavesnice pocitaca v oknach pluginov (natívne okno pluginu
// dostava klavesy priamo od systemu, JUCE ich nevidi - preto sa stav klaves zistuje cez system).
namespace KbdKeys
{
    // fyzicka klavesa podla skenkodu (rovnako ako e.code v JS - nezavisle od rozlozenia klavesnice)
    bool isScanDown (int scanCode, char fallbackChar);
    // je okno s danym natívnym handle (peer->getNativeHandle()) popredi systemu?
    bool isForeground (void* nativeHandle);
    // pise sa prave do natívneho textoveho pola (Edit) v popredi?
    bool nativeTextFocus();
    // Ctrl / Alt / Win stlacene
    bool commandModsDown();
}
