#pragma once

#include <juce_graphics/juce_graphics.h>

// Liberty Pro: shared visual tokens for the next-generation UI.
// Centralise colours and spacing; do not alter component geometry implicitly.
namespace LibertyUI
{
namespace Colour
{
inline const juce::Colour canvas      { 0xff10151d };
inline const juce::Colour panel       { 0xff101821 };
inline const juce::Colour surface     { 0xff1d2935 };
inline const juce::Colour control     { 0xff1b2833 };
inline const juce::Colour divider     { 0xff314553 };
inline const juce::Colour accent      { 0xff65e0dc };
inline const juce::Colour active      { 0xff286674 };
inline const juce::Colour selection   { 0xff23434c };
inline const juce::Colour textPrimary { 0xfff0f6f8 };
inline const juce::Colour textMuted   { 0xff91a8b7 };
inline const juce::Colour textCyan    { 0xffb8e8ec };
}
namespace Space
{
constexpr int xs = 4;
constexpr int sm = 8;
constexpr int md = 12;
constexpr int lg = 16;
constexpr int xl = 24;
constexpr float controlRadius = 5.0f;
}
}
