#include "CrashGuard.h"
#include <cstdio>
#include <cstring>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace CrashGuard
{
    std::atomic<const void*> busy { nullptr };

    namespace
    {
        struct Entry { std::atomic<const void*> ptr { nullptr }; char uid[480] {}; char name[160] {}; };
        Entry table[256];
        std::atomic<int> tableNext { 0 };
        char loadUid[480] {}, loadName[160] {};
        std::atomic<bool> loading { false };
        juce::File dir;
        char crashPath[2048] {}; char detail[700] {};
        Info last;

        void copy (char* dst, size_t n, const juce::String& s)
        {
            const auto u = s.toRawUTF8();
            std::strncpy (dst, u, n - 1); dst[n - 1] = 0;
        }

        void writeCrash (const char* uid, const char* name, const char* where)
        {
           #if JUCE_WINDOWS
            FILE* f = nullptr; fopen_s (&f, crashPath, "wb");
           #else
            FILE* f = std::fopen (crashPath, "wb");
           #endif
            if (f == nullptr) return;
            std::fputs (uid, f); std::fputc ('\n', f);
            std::fputs (name, f); std::fputc ('\n', f);
            std::fputs (where, f); std::fputc ('\n', f);
            std::fputs (detail, f); std::fputc ('\n', f);
            std::fclose (f);
        }

        void onCrash (void* info)
        {
           #if JUCE_WINDOWS
            // presne miesto pada: kod vynimky, modul a posun v nom (na najdenie chyby)
            if (auto* ep = static_cast<EXCEPTION_POINTERS*> (info); ep != nullptr && ep->ExceptionRecord != nullptr)
            {
                const auto* er = ep->ExceptionRecord;
                HMODULE mod = nullptr; char path[MAX_PATH] = "?";
                if (GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) er->ExceptionAddress, &mod))
                    GetModuleFileNameA (mod, path, MAX_PATH);
                const char* fn = std::strrchr (path, '\\'); fn = fn != nullptr ? fn + 1 : path;
                std::snprintf (detail, sizeof (detail), "code=%08lX module=%s offset=0x%llX",
                               (unsigned long) er->ExceptionCode, fn,
                               (unsigned long long) ((const char*) er->ExceptionAddress - (const char*) mod));
            }
           #else
            juce::ignoreUnused (info);
           #endif
            if (loading.load()) { writeCrash (loadUid, loadName, "load"); return; }
            if (const void* p = busy.load())
                for (auto& e : table)
                    if (e.ptr.load() == p) { writeCrash (e.uid, e.name, "play"); return; }
            writeCrash ("", "", "unknown");
        }

        juce::File runFlag()     { return dir.getChildFile ("running.flag"); }
        juce::File loadMarker()  { return dir.getChildFile ("plugin_loading.txt"); }
        juce::File crashFile()   { return dir.getChildFile ("crash.txt"); }
    }

    void install (const juce::File& dataDir)
    {
        dir = dataDir;
        copy (crashPath, sizeof (crashPath), crashFile().getFullPathName());

        // co sa stalo minule
        const bool wasRunning = runFlag().existsAsFile();
        juce::StringArray lines;
        if (crashFile().existsAsFile()) lines.addLines (crashFile().loadFileAsString());
        else if (loadMarker().existsAsFile()) { lines.addLines (loadMarker().loadFileAsString()); lines.set (2, "load"); }
        if (lines.size() >= 3 || wasRunning)
        {
            last.crashed = true;
            last.uid = lines[0].trim(); last.name = lines[1].trim(); last.where = lines[2].trim();
        }
        if (crashFile().existsAsFile()) crashFile().copyFileTo (dir.getChildFile ("crash_last.txt"));   // ostane na diagnostiku
        crashFile().deleteFile(); loadMarker().deleteFile();
        runFlag().replaceWithText ("1");
        juce::SystemStats::setApplicationCrashHandler (onCrash);
    }

    void cleanExit() { runFlag().deleteFile(); }

    Info takeLastCrash() { auto r = last; last = {}; return r; }

    void registerPlugin (const void* p, const juce::String& uid, const juce::String& name)
    {
        if (p == nullptr) return;
        Entry* e = nullptr;
        for (auto& x : table) if (x.ptr.load() == p) { e = &x; break; }
        if (e == nullptr) e = &table[(size_t) (tableNext.fetch_add (1) & 255)];
        e->ptr.store (nullptr);
        copy (e->uid, sizeof (e->uid), uid); copy (e->name, sizeof (e->name), name);
        e->ptr.store (p);
    }

    void beginLoad (const juce::String& uid, const juce::String& name)
    {
        copy (loadUid, sizeof (loadUid), uid); copy (loadName, sizeof (loadName), name);
        loading = true;
        if (dir != juce::File()) loadMarker().replaceWithText (uid + "\n" + name + "\nload\n");
    }

    void endLoad()
    {
        loading = false;
        if (dir != juce::File()) loadMarker().deleteFile();
    }
}
