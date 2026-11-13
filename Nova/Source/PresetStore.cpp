#include "PresetStore.h"
#include "Params.h"

namespace nova
{
    PresetStore::PresetStore()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Korgus Nova");
        dir = base.getChildFile ("Zvuky");
        indexFile = base.getChildFile ("zoznam.json");
        refresh();
    }

    void PresetStore::loadIndex()
    {
        order.clear(); userCats.clear(); catOverride.clear(); settings.clear();
        auto j = juce::JSON::parse (indexFile.loadFileAsString());
        if (auto* a = j["order"].getArray()) for (auto& k : *a) order.add (k.toString());
        if (auto* a = j["folders"].getArray()) for (auto& k : *a) if (k.toString().trim().isNotEmpty()) userCats.addIfNotAlreadyThere (k.toString().trim());
        if (auto* o = j["cat"].getDynamicObject()) for (auto& p : o->getProperties()) catOverride[p.name.toString()] = p.value.toString();
        if (auto* o = j["settings"].getDynamicObject()) for (auto& p : o->getProperties()) settings[p.name.toString()] = p.value.toString();
    }

    void PresetStore::saveIndex() const
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> a; for (auto& k : order) a.add (k);
        o->setProperty ("order", a);
        juce::Array<juce::var> fa; for (auto& k : userCats) fa.add (k);
        o->setProperty ("folders", fa);
        auto* c = new juce::DynamicObject();
        for (auto& [k, v] : catOverride) c->setProperty (k, v);
        o->setProperty ("cat", juce::var (c));
        auto* st = new juce::DynamicObject();
        for (auto& [k, v] : settings) st->setProperty (k, v);
        o->setProperty ("settings", juce::var (st));
        indexFile.getParentDirectory().createDirectory();
        indexFile.replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    void PresetStore::refresh()
    {
        loadIndex();
        std::vector<Entry> all;
        const auto& fac = factoryPatches();
        for (int i = 0; i < (int) fac.size(); ++i)
        {
            Entry e; e.factory = true; e.factoryIndex = i; e.name = fac[(size_t) i].name; e.category = fac[(size_t) i].category;
            e.key = "f:" + e.name;
            all.push_back (e);
        }
        if (dir.isDirectory())
            for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.knova"))
            {
                auto j = juce::JSON::parse (f.loadFileAsString());
                Entry e; e.file = f; e.key = "u:" + f.getFileName();
                e.name = j["name"].toString(); if (e.name.isEmpty()) e.name = f.getFileNameWithoutExtension();
                e.category = j["category"].toString(); if (e.category.isEmpty()) e.category = categories()[categories().size() - 1];
                all.push_back (e);
            }
        for (auto& e : all) { auto it = catOverride.find (e.key); if (it != catOverride.end()) e.category = it->second; }
        // poradie zo zoznamu, nove na koniec
        list.clear();
        for (auto& k : order)
            for (auto& e : all) if (e.key == k) { list.push_back (e); break; }
        for (auto& e : all)
            if (! order.contains (e.key)) list.push_back (e);
    }

    const PresetStore::Entry* PresetStore::find (const juce::String& key) const
    {
        for (auto& e : list) if (e.key == key) return &e;
        return nullptr;
    }

    juce::var PresetStore::toJson (const PatchData& p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", p.name); o->setProperty ("category", p.category); o->setProperty ("version", 2);
        auto* pv = new juce::DynamicObject();
        for (auto& [k, v] : p.values) pv->setProperty (k, v);
        o->setProperty ("params", juce::var (pv));
        auto* sm = new juce::DynamicObject();
        for (int i = 0; i < 2; ++i)
            if (p.sampleFlac[i].getSize() > 0)
            {
                auto* s = new juce::DynamicObject();
                s->setProperty ("name", p.sampleName[i]);
                s->setProperty ("flac", p.sampleFlac[i].toBase64Encoding());
                sm->setProperty (i == 0 ? "a" : "b", juce::var (s));
            }
        o->setProperty ("samples", juce::var (sm));
        return juce::var (o);
    }

    bool PresetStore::fromJson (const juce::var& j, PatchData& out)
    {
        auto* pv = j["params"].getDynamicObject();
        if (pv == nullptr) return false;
        out.name = j["name"].toString(); out.category = j["category"].toString();
        out.values.clear();
        for (auto& p : pv->getProperties()) out.values[p.name.toString()] = (float) (double) p.value;
        for (int i = 0; i < 2; ++i)
        {
            const auto s = j["samples"][i == 0 ? "a" : "b"];
            out.sampleFlac[i].reset(); out.sampleName[i] = {};
            if (s.isObject()) { out.sampleFlac[i].fromBase64Encoding (s["flac"].toString()); out.sampleName[i] = s["name"].toString(); }
        }
        return true;
    }

    bool PresetStore::load (const juce::String& key, PatchData& out) const
    {
        const auto* e = find (key);
        if (e == nullptr) return false;
        if (e->factory)
        {
            const auto& f = factoryPatches()[(size_t) e->factoryIndex];
            out = {};
            out.name = f.name; out.category = e->category;
            for (auto& s : paramSpecs()) if (isPatchParam (s.id)) out.values[s.id] = s.def;
            out.values["b_on"] = 0;
            for (auto& [k, v] : f.values) out.values[k] = v;
            return true;
        }
        return fromJson (juce::JSON::parse (e->file.loadFileAsString()), out);
    }

    juce::String PresetStore::save (const PatchData& p)
    {
        dir.createDirectory();
        auto name = p.name.trim(); if (name.isEmpty()) name = "Zvuk";
        auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".knova");
        file.replaceWithText (juce::JSON::toString (toJson (p)));
        const auto key = "u:" + file.getFileName();
        catOverride.erase (key);
        saveIndex();
        refresh();
        return key;
    }

    bool PresetStore::rename (const juce::String& key, const juce::String& newName)
    {
        const auto* e = find (key);
        if (e == nullptr || e->factory || newName.trim().isEmpty()) return false;
        PatchData p;
        if (! load (key, p)) return false;
        p.name = newName.trim();
        const auto oldFile = e->file;
        auto file = dir.getChildFile (juce::File::createLegalFileName (p.name) + ".knova");
        file.replaceWithText (juce::JSON::toString (toJson (p)));
        if (file != oldFile) oldFile.deleteFile();
        const auto newKey = "u:" + file.getFileName();
        const int idx = order.indexOf (key);
        if (idx >= 0) order.set (idx, newKey);
        auto it = catOverride.find (key);
        if (it != catOverride.end()) { catOverride[newKey] = it->second; catOverride.erase (key); }
        saveIndex(); refresh();
        return true;
    }

    bool PresetStore::remove (const juce::String& key)
    {
        const auto* e = find (key);
        if (e == nullptr || e->factory) return false;
        e->file.deleteFile();
        order.removeString (key); catOverride.erase (key);
        saveIndex(); refresh();
        return true;
    }

    juce::String PresetStore::duplicate (const juce::String& key)
    {
        PatchData p;
        if (! load (key, p)) return {};
        juce::String base = p.name + juce::String::fromUTF8 (" (kópia)"), name = base;
        for (int i = 2; dir.getChildFile (juce::File::createLegalFileName (name) + ".knova").existsAsFile(); ++i) name = base + " " + juce::String (i);
        p.name = name;
        if (p.category.isEmpty()) p.category = categories()[categories().size() - 1];
        return save (p);
    }

    void PresetStore::setCategory (const juce::String& key, const juce::String& cat)
    {
        const auto* e = find (key);
        if (e == nullptr) return;
        if (! e->factory)
        {
            PatchData p;
            if (load (key, p)) { p.category = cat; e->file.replaceWithText (juce::JSON::toString (toJson (p))); }
            catOverride.erase (key);
        }
        else catOverride[key] = cat;
        saveIndex(); refresh();
    }

    void PresetStore::move (const juce::String& key, const juce::String& beforeKey)
    {
        if (key == beforeKey) return;
        juce::StringArray cur;
        for (auto& e : list) cur.add (e.key);
        cur.removeString (key);
        const int at = beforeKey.isEmpty() ? -1 : cur.indexOf (beforeKey);
        cur.insert (at, key);
        order = cur;
        saveIndex(); refresh();
    }

    juce::StringArray PresetStore::allCategories() const
    {
        const auto& fac = categories();
        juce::StringArray c;
        for (int i = 0; i < fac.size() - 1; ++i) c.add (fac[i]);
        for (auto& u : userCats) c.addIfNotAlreadyThere (u);
        for (auto& e : list) if (e.category.isNotEmpty() && ! c.contains (e.category) && e.category != fac[fac.size() - 1]) c.add (e.category);
        c.add (fac[fac.size() - 1]);
        return c;
    }

    bool PresetStore::addCategory (const juce::String& name)
    {
        const auto n = name.trim();
        if (n.isEmpty() || allCategories().contains (n)) return false;
        userCats.add (n);
        saveIndex();
        return true;
    }

    bool PresetStore::renameCategory (const juce::String& from, const juce::String& to)
    {
        const auto n = to.trim();
        if (n.isEmpty() || n == from || allCategories().contains (n) || ! userCats.contains (from)) return false;
        userCats.set (userCats.indexOf (from), n);
        saveIndex();
        juce::StringArray keys;
        for (auto& e : list) if (e.category == from) keys.add (e.key);
        for (auto& k : keys) setCategory (k, n);
        refresh();
        return true;
    }

    void PresetStore::removeCategory (const juce::String& name)
    {
        userCats.removeString (name);
        saveIndex();
        juce::StringArray keys;
        for (auto& e : list) if (e.category == name) keys.add (e.key);
        for (auto& k : keys)
        {
            const auto* e = find (k);
            if (e != nullptr && e->factory) { catOverride.erase (k); saveIndex(); refresh(); }   // tovarensky sa vrati do svojej
            else setCategory (k, categories()[categories().size() - 1]);
        }
        refresh();
    }

    juce::String PresetStore::importFile (const juce::File& f)
    {
        PatchData p;
        if (! fromJson (juce::JSON::parse (f.loadFileAsString()), p)) return {};
        if (p.name.isEmpty()) p.name = f.getFileNameWithoutExtension();
        return save (p);
    }

    bool PresetStore::exportTo (const juce::String& key, const juce::File& f) const
    {
        PatchData p;
        if (! load (key, p)) return false;
        return f.replaceWithText (juce::JSON::toString (toJson (p)));
    }
}
