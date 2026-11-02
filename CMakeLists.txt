cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect on macOS.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(NDIAudioBridge VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C runtime statically so the .exe runs without installing
# the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# JUCE (audio devices + UI). Uses a local checkout if JUCE_SOURCE_DIR is set,
# otherwise downloads the pinned release.
# ---------------------------------------------------------------------------
set(JUCE_SOURCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_SOURCE_DIR)
    add_subdirectory(${JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
endif()

option(NDIAB_BUILD_TESTS "Build the headless self-test" ON)

# NDI is loaded at runtime from the installed NDI Runtime / NDI Tools, so only
# the (MIT-licensed) headers are needed to build.
set(NDIAB_NDI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ndi/include)

set(NDIAB_CORE_SOURCES
    src/ndi/NdiLibrary.cpp
    src/ndi/NdiSendHub.cpp
    src/ndi/NdiReceiver.cpp
    src/ndi/NdiFinder.cpp)

set(NDIAB_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_ALSA=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_ASIO=$<IF:$<PLATFORM_ID:Windows>,1,0>)

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
juce_add_gui_app(NDIAudioBridge
    PRODUCT_NAME "NDI Audio Bridge"
    COMPANY_NAME "NDI Audio Bridge"
    BUNDLE_ID "com.ndiaudiobridge.app"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NDI Audio Bridge needs audio input access to send your interface inputs over NDI."
    PLIST_TO_MERGE [[
<plist><dict>
  <key>NSLocalNetworkUsageDescription</key>
  <string>NDI Audio Bridge uses the local network to send and receive NDI audio.</string>
  <key>NSBonjourServices</key>
  <array><string>_ndi._tcp</string></array>
  <key>NSAppSleepDisabled</key>
  <true/>
</dict></plist>
]])

target_sources(NDIAudioBridge PRIVATE
    src/Main.cpp
    src/engine/AudioEngine.cpp
    src/engine/Settings.cpp
    src/ui/MainComponent.cpp
    ${NDIAB_CORE_SOURCES})

target_include_directories(NDIAudioBridge PRIVATE src)
target_include_directories(NDIAudioBridge SYSTEM PRIVATE ${NDIAB_NDI_INCLUDE})

target_compile_definitions(NDIAudioBridge PRIVATE
    ${NDIAB_COMMON_DEFS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NDIAudioBridge,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NDIAudioBridge,JUCE_VERSION>")

target_link_libraries(NDIAudioBridge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless self-test (drift FIFO simulation + NDI loopback if a runtime exists)
# ---------------------------------------------------------------------------
if(NDIAB_BUILD_TESTS)
    juce_add_console_app(NDIAudioBridgeSelfTest PRODUCT_NAME "NDIAudioBridgeSelfTest")
    target_sources(NDIAudioBridgeSelfTest PRIVATE tests/SelfTest.cpp src/engine/AudioEngine.cpp ${NDIAB_CORE_SOURCES})
    target_include_directories(NDIAudioBridgeSelfTest PRIVATE src)
    target_include_directories(NDIAudioBridgeSelfTest SYSTEM PRIVATE ${NDIAB_NDI_INCLUDE})
    target_compile_definitions(NDIAudioBridgeSelfTest PRIVATE ${NDIAB_COMMON_DEFS})
    target_link_libraries(NDIAudioBridgeSelfTest
        PRIVATE juce::juce_core juce::juce_audio_basics juce::juce_audio_devices
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
