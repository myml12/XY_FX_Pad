cmake_minimum_required(VERSION 3.22)

project(XY_FX_Pad VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DJXYPAD_FETCH_JUCE "Download JUCE with FetchContent when JUCE is not installed" OFF)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    find_package(JUCE CONFIG QUIET)
endif()

if (NOT TARGET juce::juce_audio_utils)
    if (DJXYPAD_FETCH_JUCE)
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.7
        )
        FetchContent_MakeAvailable(JUCE)
    else()
        message(FATAL_ERROR
            "JUCE was not found. Put a JUCE clone at ./JUCE, configure with "
            "-DJUCE_DIR=/path/to/JUCE, or use -DDJXYPAD_FETCH_JUCE=ON if "
            "network access is available.")
    endif()
endif()

juce_add_gui_app(DJXYPad
    PRODUCT_NAME "DJ XY Pad"
    COMPANY_NAME "Local"
    BUNDLE_ID "local.dj.xypad"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DJ XY Pad needs audio input access to capture system audio via BlackHole and apply effects."
)

target_sources(DJXYPad PRIVATE
    Source/ControllerReceiver.cpp
    Source/Effects.cpp
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/SystemAudioRouter.cpp
    Source/Tempo.cpp
    Source/XYPad.cpp
)

target_compile_definitions(DJXYPad PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    SOURCE_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/audio"
)

target_link_libraries(DJXYPad PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
)

if (APPLE)
    target_link_libraries(DJXYPad PRIVATE "-framework CoreAudio")
endif()

file(GLOB DJXYPAD_AUDIO_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.m4a"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.mp3"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.wav"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.aiff"
)

if (DJXYPAD_AUDIO_FILES)
    add_custom_command(TARGET DJXYPad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:DJXYPad>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DJXYPAD_AUDIO_FILES} "$<TARGET_BUNDLE_CONTENT_DIR:DJXYPad>/Resources"
        VERBATIM
    )
endif()

add_executable(ControllerBridge
    Tools/ControllerBridge.cpp
)

target_compile_features(ControllerBridge PRIVATE cxx_std_17)
