#pragma once
#include <juce_core/juce_core.h>

// Captures a native window belonging to Liberty with ScreenCaptureKit (macOS 14+).
// Returns false with an explanation rather than storing an unrelated or blank image.
bool captureLibertyPluginWindowNative(const juce::String& pluginName,
                                      const juce::File& output,
                                      juce::String& error);
