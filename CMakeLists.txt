cmake_minimum_required(VERSION 3.22)

project(BSM_DAW VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(BSM_DAW
    PRODUCT_NAME "BSM DAW"
    COMPANY_NAME "BSM / Bijou Studio Music"
    VERSION "0.1.0"
    BUNDLE_ID "com.bsm.bsm-daw"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

target_sources(BSM_DAW PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/Editing.cpp
    Source/TransportSettings.cpp
)

target_include_directories(BSM_DAW PRIVATE
    Source
)

target_compile_features(BSM_DAW PRIVATE cxx_std_17)

target_compile_definitions(BSM_DAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(BSM_DAW PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

juce_generate_juce_header(BSM_DAW)
