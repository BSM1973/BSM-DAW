#pragma once
// JUCE GUI strings assembled from Unicode code points, not raw source-encoded
// bytes. This avoids mangled French characters on both macOS and Windows.
#include <juce_core/juce_core.h>

namespace LibertyPolyGridFrench
{
inline juce::String cp(int codepoint) { return juce::String::charToString(codepoint); }
inline juce::String velocity()
{
    return juce::String("V") + cp(0x00C9) + "LOCIT" + cp(0x00C9);
}
inline juce::String offset()
{
    return juce::String("D") + cp(0x00C9) + "CALAGE";
}
inline juce::String duration()
{
    return juce::String("DUR") + cp(0x00C9) + "E";
}
inline juce::String repeat()
{
    return juce::String("R") + cp(0x00C9) + "P" + cp(0x00C9) + "TITION";
}
inline juce::String redo()
{
    return juce::String("R") + cp(0x00C9) + "TABLIR";
}
inline juce::String insert()
{
    return juce::String("INS") + cp(0x00C9) + "RER ARR.";
}
inline juce::String inserted()
{
    return juce::String("CLIP INS") + cp(0x00C9) + "R" + cp(0x00C9);
}
inline juce::String stop()
{
    return juce::String("ARR") + cp(0x00CA) + "T";
}
inline juce::String selectNote()
{
    return juce::String("S") + cp(0x00C9) + "LECTIONNER UNE NOTE, PUIS GLISSER HAUT / BAS";
}
inline juce::String detachedHint()
{
    return juce::String("Ouvrez le Step Sequencer en fen")
        + cp(0x00EA) + "tre d" + cp(0x00E9) + "tach" + cp(0x00E9) + "e";
}
inline juce::String clipError()
{
    return juce::String("Impossible d'ins") + cp(0x00E9)
         + "rer le clip dans l'arrangement. V"
         + cp(0x00E9) + "rifiez la position et les clips existants.";
}
inline juce::String noInstrument()
{
    return juce::String("Chargez un instrument virtuel sur la piste instrument cible avant la lecture.");
}
inline juce::String emptyPattern()
{
    return juce::String("Cr") + cp(0x00E9) + "ez au moins une note dans le pattern.";
}
inline bool correctUnicode() noexcept
{
    return velocity()[1] == 0x00C9 && redo()[1] == 0x00C9 &&
           offset()[1] == 0x00C9 && stop()[3] == 0x00CA &&
           insert()[3] == 0x00C9;
}
}
