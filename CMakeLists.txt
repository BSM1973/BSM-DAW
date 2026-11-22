cmake_minimum_required(VERSION 3.24)
project(BSM_DAW VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
FetchContent_Declare(signalsmith-stretch GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git GIT_TAG main GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE signalsmith-stretch)
# Pinned public-domain (CC0 1.0) recorded and designed drum one-shots.
# Sources, creators and exact source checksums are documented in the pack.
# Bundled into Liberty with JUCE BinaryData: no runtime network dependency,
# and no redistribution of incompatible commercial sample-pack licenses.
FetchContent_Declare(liberty_factory_drums
 GIT_REPOSITORY https://github.com/august-byrne/phazite-sounds.git
 GIT_TAG bd71faf5035e8aa278ebfe3f4dc68804ae7bc5ca
 GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(liberty_factory_drums)
file(GLOB_RECURSE LIBERTY_DRUM_RECORDINGS CONFIGURE_DEPENDS
 "${liberty_factory_drums_SOURCE_DIR}/pack/Drums/*.flac"
 "${liberty_factory_drums_SOURCE_DIR}/pack/808s/*.flac")
set(LIBERTY_DRUM_SPECIALS
 "${liberty_factory_drums_SOURCE_DIR}/pack/FX/Impact Boom.flac"
 "${liberty_factory_drums_SOURCE_DIR}/pack/FX/Impact Concert Drum.flac"
 "${liberty_factory_drums_SOURCE_DIR}/pack/FX/Impact Hit.flac"
 "${liberty_factory_drums_SOURCE_DIR}/pack/FX/Gong.flac"
 "${liberty_factory_drums_SOURCE_DIR}/pack/FX/Whoosh.flac")
list(LENGTH LIBERTY_DRUM_RECORDINGS LIBERTY_DRUM_COUNT)
if(LIBERTY_DRUM_COUNT LESS 40)
 message(FATAL_ERROR "Liberty Drum Studio requires the CC0 factory sample library")
endif()
juce_add_binary_data(LibertyFactoryDrumAssets
 HEADER_NAME "LibertyFactoryDrumData.h"
 NAMESPACE LibertyDrumAssets
 SOURCES ${LIBERTY_DRUM_RECORDINGS} ${LIBERTY_DRUM_SPECIALS})
juce_add_gui_app(BSM_DAW PRODUCT_NAME "Liberty" COMPANY_NAME "BSM / Bijou Studio Music" VERSION "0.1.0" BUNDLE_ID "com.bsm.liberty" NEEDS_CURL FALSE NEEDS_WEB_BROWSER TRUE MICROPHONE_PERMISSION_ENABLED TRUE MICROPHONE_PERMISSION_TEXT "Liberty needs access to your audio interface inputs for audio recording." PLIST_TO_MERGE "<key>NSScreenCaptureUsageDescription</key><string>Liberty captures plugin interfaces locally to display accurate thumbnails in its browser.</string>")
target_sources(BSM_DAW PRIVATE
 Source/Main.cpp Source/MainComponent.cpp Source/AudioArrangement.cpp Source/AudioEngine.cpp Source/MidiEngine.cpp Source/StepSequencer.cpp Source/MidiQuantize.cpp Source/MidiEditor.cpp
 Source/MidiVelocityInteraction.cpp Source/MidiNoteSelectionInteraction.cpp Source/MidiGroupDragInteraction.cpp Source/MidiMarqueeOverlay.cpp Source/MidiMarqueeManager.cpp Source/MidiSelectAllInteraction.cpp Source/MidiQuantizeInteraction.cpp Source/MidiKeyboardRouter.cpp
 Source/Editing.cpp Source/Project.cpp Source/TransportSettings.cpp Source/AudioRecordingController.cpp Source/TrackColourInteraction.cpp Source/MultiMidiClipController.cpp Source/GridSnapController.cpp Source/TrackHeaderMixControls.cpp Source/TrackPluginInsertControls.cpp Source/PluginDragDropController.cpp
 Source/MixConsoleController.cpp Source/PerformController.cpp Source/PageModeCoordinator.cpp Source/UISymbolCleaner.cpp Source/SpliceBrowserController.cpp Source/BrowserResizeController.cpp Source/UILayerCoordinator.cpp Source/AudioSettingsRelocator.cpp Source/LanguageController.cpp Source/ToolPaletteController.cpp
 Source/LibertyAIController.cpp Source/LibertyAIAdvancedController.cpp Source/AIRenderController.cpp Source/AINoveltyController.cpp Source/AIArrangerController.cpp Source/AISongIntelligenceController.cpp Source/AIComposer2Controller.cpp
 Source/OneKnobEffects.cpp Source/DynamicTrackController.cpp Source/ZoomController.cpp Source/AudioClipWarpView.cpp Source/AudioClipResizeEngine.cpp Source/AudioClipResizeInteraction.cpp Source/MetronomeController.cpp Source/BrowserController.cpp Source/PluginHost.cpp)
if(APPLE)
  target_sources(BSM_DAW PRIVATE Source/NativePluginCapture.mm)
  set_source_files_properties(Source/NativePluginCapture.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(BSM_DAW PRIVATE "-framework ScreenCaptureKit" "-framework AppKit" "-framework CoreGraphics")
endif()
target_include_directories(BSM_DAW PRIVATE Source)
target_compile_features(BSM_DAW PRIVATE cxx_std_17)
# Keep French UI literals consistently encoded on all supported compilers.
if(MSVC)
  target_compile_options(BSM_DAW PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(BSM_DAW PRIVATE -finput-charset=UTF-8)
endif()
target_compile_definitions(BSM_DAW PRIVATE JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1 SIGNALSMITH_USE_ACCELERATE=1)
target_link_libraries(BSM_DAW PRIVATE LibertyFactoryDrumAssets)
target_link_libraries(BSM_DAW PRIVATE juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_processors juce::juce_audio_utils juce::juce_dsp juce::juce_core juce::juce_data_structures juce::juce_events juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra signalsmith-stretch)
juce_generate_juce_header(BSM_DAW)
# Lightweight headless regression coverage for the Drum Sampler.
juce_add_console_app(LibertyDrumSamplerSmoke PRODUCT_NAME "Liberty Drum Sampler Smoke")
target_sources(LibertyDrumSamplerSmoke PRIVATE Tests/DrumSamplerSmoke.cpp)
target_include_directories(LibertyDrumSamplerSmoke PRIVATE Source)
target_compile_features(LibertyDrumSamplerSmoke PRIVATE cxx_std_17)
target_compile_definitions(LibertyDrumSamplerSmoke PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(LibertyDrumSamplerSmoke PRIVATE LibertyFactoryDrumAssets)
target_link_libraries(LibertyDrumSamplerSmoke PRIVATE
    juce::juce_audio_basics juce::juce_audio_formats juce::juce_gui_basics
    juce::juce_core juce::juce_events juce::juce_graphics)
enable_testing()
add_test(NAME DrumSamplerSmoke COMMAND LibertyDrumSamplerSmoke)

# Arrangement clip note partitioning, independent of devices and GUI.
add_executable(LibertyMidiClipEditingSmoke Tests/MidiClipEditingSmoke.cpp)
target_include_directories(LibertyMidiClipEditingSmoke PRIVATE Source)
# Keep assertions active in Release for this regression executable.
if(MSVC)
  target_compile_options(LibertyMidiClipEditingSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyMidiClipEditingSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME MidiClipEditingSmoke COMMAND LibertyMidiClipEditingSmoke)

# Phase 1 polyphonic Step Sequencer: JUCE-free deterministic eight-lane model.
add_executable(LibertyPolyphonicGridSmoke Tests/PolyphonicStepGridSmoke.cpp)
target_include_directories(LibertyPolyphonicGridSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridSmoke COMMAND LibertyPolyphonicGridSmoke)

add_executable(LibertyPolyphonicGridLayoutSmoke Tests/PolyphonicStepGridLayoutSmoke.cpp)
target_include_directories(LibertyPolyphonicGridLayoutSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridLayoutSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridLayoutSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridLayoutSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridLayoutSmoke COMMAND LibertyPolyphonicGridLayoutSmoke)

add_executable(LibertyPolyphonicGridEditingSmoke Tests/PolyphonicStepGridEditingSmoke.cpp)
target_include_directories(LibertyPolyphonicGridEditingSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridEditingSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridEditingSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridEditingSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridEditingSmoke COMMAND LibertyPolyphonicGridEditingSmoke)

add_executable(LibertyPolyphonicGridMidiBridgeSmoke Tests/PolyphonicStepGridMidiBridgeSmoke.cpp)
target_include_directories(LibertyPolyphonicGridMidiBridgeSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridMidiBridgeSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridMidiBridgeSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridMidiBridgeSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridMidiBridgeSmoke COMMAND LibertyPolyphonicGridMidiBridgeSmoke)

add_executable(LibertyPolyphonicGridProfessionalSmoke Tests/PolyphonicStepGridProfessionalSmoke.cpp)
target_include_directories(LibertyPolyphonicGridProfessionalSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridProfessionalSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridProfessionalSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridProfessionalSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridProfessionalSmoke COMMAND LibertyPolyphonicGridProfessionalSmoke)

add_executable(LibertyPolyphonicGridDirectEditSmoke Tests/PolyphonicStepGridDirectEditSmoke.cpp)
target_include_directories(LibertyPolyphonicGridDirectEditSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridDirectEditSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridDirectEditSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridDirectEditSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridDirectEditSmoke COMMAND LibertyPolyphonicGridDirectEditSmoke)

add_executable(LibertyPolyphonicGridPagesSmoke Tests/PolyphonicStepGridPagesSmoke.cpp)
target_include_directories(LibertyPolyphonicGridPagesSmoke PRIVATE Source)
target_compile_features(LibertyPolyphonicGridPagesSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyPolyphonicGridPagesSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyPolyphonicGridPagesSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME PolyphonicGridPagesSmoke COMMAND LibertyPolyphonicGridPagesSmoke)

# Compact dock Mixer: the fader and every signed dB mark share one gain law.
add_executable(LibertyDockMixerDbScaleSmoke Tests/DockMixerDbScaleSmoke.cpp)
target_include_directories(LibertyDockMixerDbScaleSmoke PRIVATE Source)
target_compile_features(LibertyDockMixerDbScaleSmoke PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(LibertyDockMixerDbScaleSmoke PRIVATE /UNDEBUG)
else()
  target_compile_options(LibertyDockMixerDbScaleSmoke PRIVATE -UNDEBUG)
endif()
add_test(NAME DockMixerDbScaleSmoke COMMAND LibertyDockMixerDbScaleSmoke)

add_test(NAME AudioClipPlaybackSmoke COMMAND BSM_DAW --audio-clip-playback-test)

add_test(NAME ArrangementToolsSmoke COMMAND BSM_DAW --arrangement-tools-test)
set_tests_properties(ArrangementToolsSmoke PROPERTIES TIMEOUT 60)
