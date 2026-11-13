#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

// Zvuky Korgus Nova: tovarenske (v programe) + vlastne (subory .knova v Dokumenty/Korgus Nova/Zvuky)
namespace nova
{
    struct PatchData
    {
        juce::String name, category;
        std::map<juce::String, float> values;            // vsetky parametre zvuku
        juce::MemoryBlock sampleFlac[2]; juce::String sampleName[2];   // vlastne WAV vo vrstvach (FLAC)
    };

    class PresetStore
    {
    public:
        struct Entry { juce::String key, name, category; bool factory = false; int factoryIndex = -1; juce::File file; };

        PresetStore();
        juce::File folder() const { return dir; }
        void refresh();
        const std::vector<Entry>& entries() const { return list; }
        const Entry* find (const juce::String& key) const;

        bool load (const juce::String& key, PatchData& out) const;
        juce::String save (const PatchData& p);           // vrati kluc; rovnaky nazov prepise
        bool rename (const juce::String& key, const juce::String& newName);
        bool remove (const juce::String& key);
        juce::String duplicate (const juce::String& key);
        void setCategory (const juce::String& key, const juce::String& cat);
        void move (const juce::String& key, const juce::String& beforeKey);   // beforeKey prazdny = na koniec
        juce::String importFile (const juce::File& f);

        // oblubene zvuky (hviezdicka)
        bool isFav (const juce::String& key) const { return favs.contains (key); }
        void setFav (const juce::String& key, bool on) { if (on) favs.addIfNotAlreadyThere (key); else favs.removeString (key); saveIndex(); }

        // kategorie = priecinky v zozname: tovarenske + vlastne (+ "Moje zvuky" na konci)
        juce::StringArray allCategories() const;
        bool isUserCategory (const juce::String& c) const { return userCats.contains (c); }
        bool addCategory (const juce::String& name);
        bool renameCategory (const juce::String& from, const juce::String& to);
        void removeCategory (const juce::String& name);       // zvuky z neho idu do "Moje zvuky"
        bool exportTo (const juce::String& key, const juce::File& f) const;

        // drobne nastavenia (napr. posledny priecinok s WAV)
        juce::String setting (const juce::String& k) const { auto it = settings.find (k); return it != settings.end() ? it->second : juce::String(); }
        void setSetting (const juce::String& k, const juce::String& v) { settings[k] = v; saveIndex(); }

        static juce::var toJson (const PatchData& p);
        static bool fromJson (const juce::var& v, PatchData& out);

    private:
        void loadIndex();
        void saveIndex() const;
        juce::File dir, indexFile;
        juce::StringArray order, userCats, favs;
        std::map<juce::String, juce::String> catOverride, settings;
        std::vector<Entry> list;
    };
}
