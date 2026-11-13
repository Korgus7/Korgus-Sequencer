#pragma once
#include <juce_core/juce_core.h>

// Jazyk Korgus Inspire (slovencina / anglictina) a priecinok s datami
namespace nova
{
    // 0 = slovencina, 1 = anglictina; prvy raz podla nastavenia alebo jazyka Windows
    int lang();
    void setLang (int l);                    // ulozi do nastaveni
    // preklad slovenskeho textu (UTF-8); v slovencine alebo bez prekladu vrati povodny text
    juce::String T (const char* sk);
    juce::String T (const juce::String& sk);
    // pre slova s dvoma vyznamami: preklad priamo na mieste
    inline juce::String T (const char* sk, const char* en) { return lang() == 1 ? juce::String::fromUTF8 (en) : juce::String::fromUTF8 (sk); }
    // Dokumenty/Korgus Inspire (pri prvom spusteni presunie stary priecinok Korgus Nova)
    juce::File dataDir();
}
