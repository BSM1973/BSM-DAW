#pragma once
#include <juce_core/juce_core.h>

// Keep CoreGraphics APIs in the .mm translation unit: importing Apple's headers
// into the JUCE browser .cpp conflicts with juce::Point on macOS 26.
bool libertyHasScreenCapturePermission();
bool libertyRequestScreenCapturePermission();

// Captures a native window belonging to Liberty with ScreenCaptureKit (macOS 14+).
// Returns false with an explanation rather than storing an unrelated or blank image.
bool captureLibertyPluginWindowNative(const juce::String& pluginName,
                                      const juce::File& output,
                                      juce::String& error);
