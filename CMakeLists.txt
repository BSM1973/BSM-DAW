cmake_minimum_required(VERSION 3.22)

project(BSM_DAW VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE will be added as the dependency in the next foundation milestone.
# Keeping the first build dependency-free gives us a clean repository baseline.

add_executable(BSM_DAW
    Source/Main.cpp
)

target_compile_features(BSM_DAW PRIVATE cxx_std_17)

if(APPLE)
    target_link_libraries(BSM_DAW PRIVATE
        "-framework Cocoa"
    )
endif()
