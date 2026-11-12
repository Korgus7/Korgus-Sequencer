// MP4 (H.264 + AAC) cez Windows Media Foundation - samostatny subor kvoli windows.h
#ifdef _WIN32
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfreadwrite.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "ole32.lib")

namespace
{
    template <typename T> void safeRelease (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }

    std::string hr2s (const char* what, HRESULT hr)
    {
        char b[160];
        snprintf (b, sizeof (b), "%s (0x%08lX)", what, (unsigned long) hr);
        return b;
    }
}

bool mfEncodeMp4 (const wchar_t* path, int W, int H, int fps, int numFrames, int sampleRate, const int16_t* pcm, int64_t numSampleFrames,
                  const std::function<bool (int, uint8_t*, int)>& getFrame, std::string& err)
{
    const HRESULT co = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    HRESULT hr = MFStartup (MF_VERSION);
    if (FAILED (hr)) { err = hr2s ("Media Foundation sa nedá spustiť", hr); if (SUCCEEDED (co)) CoUninitialize(); return false; }

    IMFSinkWriter* writer = nullptr;
    IMFAttributes* attr = nullptr;
    IMFMediaType *vOut = nullptr, *vIn = nullptr, *aOut = nullptr, *aIn = nullptr;
    DWORD vIdx = 0, aIdx = 0;
    bool ok = false, cancelled = false;
    const LONGLONG frameDur = 10000000LL / fps;

    auto fail = [&] (const char* what) { err = hr2s (what, hr); };

    do
    {
        hr = MFCreateAttributes (&attr, 2);                                      if (FAILED (hr)) { fail ("MFCreateAttributes"); break; }
        attr->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attr->SetUINT32 (MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        hr = MFCreateSinkWriterFromURL (path, nullptr, attr, &writer);           if (FAILED (hr)) { fail ("Súbor MP4 sa nedá vytvoriť"); break; }

        // video H.264
        hr = MFCreateMediaType (&vOut);                                          if (FAILED (hr)) { fail ("MFCreateMediaType"); break; }
        vOut->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        vOut->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
        vOut->SetUINT32 (MF_MT_AVG_BITRATE, W * H >= 1920 * 1080 ? 8000000u : 5000000u);
        vOut->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize (vOut, MF_MT_FRAME_SIZE, (UINT32) W, (UINT32) H);
        MFSetAttributeRatio (vOut, MF_MT_FRAME_RATE, (UINT32) fps, 1);
        MFSetAttributeRatio (vOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream (vOut, &vIdx);                                    if (FAILED (hr)) { fail ("Kodér H.264 nie je dostupný"); break; }

        hr = MFCreateMediaType (&vIn);                                           if (FAILED (hr)) { fail ("MFCreateMediaType"); break; }
        vIn->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        vIn->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        vIn->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        vIn->SetUINT32 (MF_MT_DEFAULT_STRIDE, (UINT32) (W * 4));                 // zhora nadol
        MFSetAttributeSize (vIn, MF_MT_FRAME_SIZE, (UINT32) W, (UINT32) H);
        MFSetAttributeRatio (vIn, MF_MT_FRAME_RATE, (UINT32) fps, 1);
        MFSetAttributeRatio (vIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType (vIdx, vIn, nullptr);                     if (FAILED (hr)) { fail ("Vstup videa (RGB32) sa nedá nastaviť"); break; }

        // zvuk AAC 192 kbps
        hr = MFCreateMediaType (&aOut);                                          if (FAILED (hr)) { fail ("MFCreateMediaType"); break; }
        aOut->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        aOut->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
        aOut->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        aOut->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) sampleRate);
        aOut->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
        aOut->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        hr = writer->AddStream (aOut, &aIdx);                                    if (FAILED (hr)) { fail ("Kodér AAC nie je dostupný"); break; }

        hr = MFCreateMediaType (&aIn);                                           if (FAILED (hr)) { fail ("MFCreateMediaType"); break; }
        aIn->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        aIn->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
        aIn->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        aIn->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) sampleRate);
        aIn->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
        aIn->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        aIn->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) sampleRate * 4);
        hr = writer->SetInputMediaType (aIdx, aIn, nullptr);                     if (FAILED (hr)) { fail ("Vstup zvuku sa nedá nastaviť"); break; }

        hr = writer->BeginWriting();                                             if (FAILED (hr)) { fail ("BeginWriting"); break; }

        const DWORD frameBytes = (DWORD) (W * H * 4);
        int64_t audioPos = 0;
        bool good = true;
        for (int f = 0; f < numFrames && good; ++f)
        {
            // zvuk do konca tejto snimky
            const int64_t audioEnd = std::min<int64_t> (numSampleFrames, (int64_t) ((double) (f + 1) * sampleRate / fps));
            if (audioEnd > audioPos)
            {
                const DWORD bytes = (DWORD) ((audioEnd - audioPos) * 4);
                IMFMediaBuffer* ab = nullptr; IMFSample* as = nullptr; BYTE* p = nullptr;
                hr = MFCreateMemoryBuffer (bytes, &ab);
                if (SUCCEEDED (hr)) hr = ab->Lock (&p, nullptr, nullptr);
                if (SUCCEEDED (hr)) { memcpy (p, pcm + audioPos * 2, bytes); ab->Unlock(); ab->SetCurrentLength (bytes); }
                if (SUCCEEDED (hr)) hr = MFCreateSample (&as);
                if (SUCCEEDED (hr)) hr = as->AddBuffer (ab);
                if (SUCCEEDED (hr)) hr = as->SetSampleTime ((LONGLONG) (audioPos * 10000000LL / sampleRate));
                if (SUCCEEDED (hr)) hr = as->SetSampleDuration ((LONGLONG) ((audioEnd - audioPos) * 10000000LL / sampleRate));
                if (SUCCEEDED (hr)) hr = writer->WriteSample (aIdx, as);
                safeRelease (as); safeRelease (ab);
                if (FAILED (hr)) { fail ("Zápis zvuku zlyhal"); good = false; break; }
                audioPos = audioEnd;
            }
            // snimka
            IMFMediaBuffer* vb = nullptr; IMFSample* vs = nullptr; BYTE* p = nullptr;
            hr = MFCreateMemoryBuffer (frameBytes, &vb);
            if (SUCCEEDED (hr)) hr = vb->Lock (&p, nullptr, nullptr);
            if (SUCCEEDED (hr))
            {
                const bool cont = getFrame (f, p, W * 4);
                vb->Unlock();
                vb->SetCurrentLength (frameBytes);
                if (! cont) { cancelled = true; safeRelease (vb); good = false; break; }
            }
            if (SUCCEEDED (hr)) hr = MFCreateSample (&vs);
            if (SUCCEEDED (hr)) hr = vs->AddBuffer (vb);
            if (SUCCEEDED (hr)) hr = vs->SetSampleTime ((LONGLONG) f * frameDur);
            if (SUCCEEDED (hr)) hr = vs->SetSampleDuration (frameDur);
            if (SUCCEEDED (hr)) hr = writer->WriteSample (vIdx, vs);
            safeRelease (vs); safeRelease (vb);
            if (FAILED (hr)) { fail ("Zápis videa zlyhal"); good = false; }
        }
        if (! good) break;
        hr = writer->Finalize();                                                 if (FAILED (hr)) { fail ("Dokončenie MP4 zlyhalo"); break; }
        ok = true;
    } while (false);

    safeRelease (aIn); safeRelease (aOut); safeRelease (vIn); safeRelease (vOut);
    safeRelease (writer); safeRelease (attr);
    MFShutdown();
    if (SUCCEEDED (co)) CoUninitialize();
    if (cancelled) { err = "cancel"; ok = false; }
    if (! ok) DeleteFileW (path);
    return ok;
}
#endif
