// test hotoveho VST3: nacita plugin ako hostitel, zahra ton klaviru a pada a zmeria vystup
#include <juce_audio_processors/juce_audio_processors.h>
using namespace juce;
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 2;
    AudioPluginFormatManager fm; fm.addFormat (new VST3PluginFormat());
    OwnedArray<PluginDescription> found;
    VST3PluginFormat vst3; vst3.findAllTypesForFile (found, argv[1]);
    if (found.isEmpty()) { printf ("plugin sa nenasiel\n"); return 3; }
    String err;
    auto p = fm.createPluginInstance (*found[0], 48000, 512, err);
    if (p == nullptr) { printf ("chyba: %s\n", err.toRawUTF8()); return 4; }
    printf ("plugin: %s, programov %d\n", p->getName().toRawUTF8(), p->getNumPrograms());
    p->setPlayConfigDetails (0, 2, 48000, 512);
    p->prepareToPlay (48000, 512);
    Thread::sleep (4000);   // klavir sa nacitava na pozadi
    int fails = 0;
    for (int prog : { 0, 3, 7 })
    {
        p->setCurrentProgram (prog);
        float peak = 0;
        for (int b = 0; b < 200; ++b)
        {
            AudioBuffer<float> buf (2, 512); buf.clear(); MidiBuffer mb;
            if (b == 2) mb.addEvent (MidiMessage::noteOn (1, 60, 0.8f), 0);
            if (b == 120) mb.addEvent (MidiMessage::noteOff (1, 60), 0);
            p->processBlock (buf, mb);
            peak = jmax (peak, buf.getMagnitude (0, 0, 512));
        }
        printf ("program %d (%s): spicka %.3f\n", prog, p->getProgramName (prog).toRawUTF8(), peak);
        if (! (peak > 0.01f && peak < 1.01f)) ++fails;
    }
    p->releaseResources();
    return fails == 0 ? 0 : 5;
}
