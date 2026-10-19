cmake_minimum_required(VERSION 3.22)
project(KingsIntoClowns VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable unnecessary JUCE extras for faster compile
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Dependencies: use local copies in deps/ when present, otherwise fetch the pinned versions
include(FetchContent)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE/CMakeLists.txt")
    add_subdirectory(deps/JUCE EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(juce)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/clap-juce-extensions/CMakeLists.txt")
    set(RIC_CJE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/clap-juce-extensions")
    add_subdirectory(deps/clap-juce-extensions EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG c1a5ad025f95d01e03267857fa8276ebeed16500)
    FetchContent_GetProperties(clap_juce_extensions)
    if(NOT clap_juce_extensions_POPULATED)
        FetchContent_Populate(clap_juce_extensions)
        add_subdirectory(${clap_juce_extensions_SOURCE_DIR} ${clap_juce_extensions_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
    set(RIC_CJE_DIR "${clap_juce_extensions_SOURCE_DIR}")
endif()

juce_add_plugin(KingsIntoClowns
    COMPANY_NAME "mtyas"
    BUNDLE_ID "com.mtyas.kingsintoclowns"
    PLUGIN_MANUFACTURER_CODE Mtya
    PLUGIN_CODE KiCl
    FORMATS VST3 Standalone
    PRODUCT_NAME "Kings into Clowns"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

# Enable CLAP plugin format with polyphonic modulation & note expressions
clap_juce_extensions_plugin(TARGET KingsIntoClowns
    CLAP_ID "com.mtyas.kingsintoclowns"
    CLAP_FEATURES instrument "audio-effect" synthesizer "physical-modeling" granular ambient
)

set(RIC_SOURCES
    Source/Core/StmDspCore.h
    Source/Core/SubBlockAdapter.h
    Source/Core/LockFreeVisualFifo.h
    Source/Core/ParameterDefinitions.h
    Source/Core/ClapPolyParameter.h
    Source/Core/FactoryPresets.h
    Source/Core/MidiLearnManager.h
    Source/Core/PresetManager.h

    Source/DSP/Wrappers/PlaitsWrapper.h
    Source/DSP/Wrappers/BraidsWrapper.h
    Source/DSP/Wrappers/RingsWrapper.h
    Source/DSP/Wrappers/CloudsWrapper.h
    Source/DSP/Wrappers/MarblesWrapper.h

    Source/DSP/Modulation/EnvGenerator.h
    Source/DSP/Modulation/PolyLFO.h
    Source/DSP/Modulation/ModMatrix.h

    Source/DSP/SynthVoice.h
    Source/DSP/SynthVoice.cpp
    Source/DSP/SynthEngine.h
    Source/DSP/SynthEngine.cpp

    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp

    Source/UI/PoeticLookAndFeel.h
    Source/UI/PoeticLookAndFeel.cpp
    Source/UI/PoeticKnob.h
    Source/UI/Widgets.h
    Source/UI/JourneyCanvas.h
    Source/UI/TidesPanel.h
    Source/UI/PresetBrowserModal.h
    Source/UI/AboutOverlay.h
)

target_sources(KingsIntoClowns PRIVATE ${RIC_SOURCES})

target_include_directories(KingsIntoClowns
    PRIVATE
        Source
        ${RIC_CJE_DIR}/include
)

target_compile_definitions(KingsIntoClowns
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        _USE_MATH_DEFINES=1
)

if(MSVC)
    target_compile_options(KingsIntoClowns PRIVATE /arch:AVX2 /fp:fast /W3)
else()
    target_compile_options(KingsIntoClowns PRIVATE -O3 -ffast-math)
endif()

target_link_libraries(KingsIntoClowns
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        clap_juce_extensions
)

# Automated DSP & Plugin Verification Test Suite
add_executable(kings_into_clowns_test
    tests/dsp_verification_test.cpp
    Source/DSP/SynthVoice.cpp
    Source/DSP/SynthEngine.cpp
)

target_include_directories(kings_into_clowns_test
    PRIVATE
        Source
        ${RIC_CJE_DIR}/include
)

target_compile_definitions(kings_into_clowns_test
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        _USE_MATH_DEFINES=1
)

target_link_libraries(kings_into_clowns_test
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_core
        clap_juce_extensions
)

# Dev tool: off-screen editor snapshots (cmake -DRIC_BUILD_UI_SNAPSHOT=ON)
option(RIC_BUILD_UI_SNAPSHOT "Build the off-screen UI snapshot tool" OFF)
if(RIC_BUILD_UI_SNAPSHOT)
    add_executable(ric_ui_snapshot tests/ui_snapshot.cpp)
    # Reuse the plugin's compiled shared code (JUCE modules included) with its include paths & defines
    target_include_directories(ric_ui_snapshot PRIVATE $<TARGET_PROPERTY:KingsIntoClowns,INCLUDE_DIRECTORIES>)
    target_compile_definitions(ric_ui_snapshot PRIVATE $<TARGET_PROPERTY:KingsIntoClowns,COMPILE_DEFINITIONS>)
    target_link_libraries(ric_ui_snapshot PRIVATE KingsIntoClowns)
endif()

# Dev tool: hosts the built VST3 to catch load / shutdown problems (cmake -DRIC_BUILD_HOST_SMOKE=ON)
option(RIC_BUILD_HOST_SMOKE "Build the VST3 host smoke-test tool" OFF)
if(RIC_BUILD_HOST_SMOKE)
    add_executable(ric_host_smoke tests/host_smoke.cpp)
    target_compile_definitions(ric_host_smoke PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ric_host_smoke PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
                          juce::juce_recommended_config_flags)
endif()
