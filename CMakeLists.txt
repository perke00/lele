# ATLAS - Kick Designer, Tuner & Layering Plugin
# Targets: Windows (MSVC) and Linux. macOS is intentionally not supported.
cmake_minimum_required(VERSION 3.22)
project(Atlas VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    message(FATAL_ERROR "ATLAS does not support macOS. Build on Windows or Linux.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Framework-independent DSP core (header-only) -------------------------------------------
add_library(atlas_dsp INTERFACE)
target_include_directories(atlas_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(atlas_dsp INTERFACE cxx_std_20)

# --- Automated tests ------------------------------------------------------------------------
option(ATLAS_BUILD_TESTS "Build DSP unit tests" ON)
if(ATLAS_BUILD_TESTS)
    enable_testing()
    add_executable(atlas_tests tests/main.cpp tests/test_pitch.cpp tests/test_realtime.cpp)
    target_link_libraries(atlas_tests PRIVATE atlas_dsp)
    if(MSVC)
        target_compile_options(atlas_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(atlas_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME atlas_dsp_tests COMMAND atlas_tests)
    if(NOT WIN32)
        install(TARGETS atlas_tests RUNTIME DESTINATION bin COMPONENT Runtime)
    endif()
endif()

# --- VST3 plugin (JUCE) ---------------------------------------------------------------------
# ON by default on Windows (FL Studio). On Linux it needs JUCE's system dev libraries.
if(WIN32)
    set(_atlas_plugin_default ON)
else()
    set(_atlas_plugin_default OFF)
endif()
option(ATLAS_BUILD_PLUGIN "Build the ATLAS VST3 plugin (downloads JUCE)" ${_atlas_plugin_default})

if(ATLAS_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(Atlas
        PRODUCT_NAME "ATLAS"
        COMPANY_NAME "ATLAS"
        PLUGIN_MANUFACTURER_CODE Atls
        PLUGIN_CODE Atl1
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Analyzer)

    target_sources(Atlas PRIVATE
        plugin/AtlasProcessor.cpp plugin/AtlasProcessor.h
        plugin/AtlasEditor.cpp plugin/AtlasEditor.h)
    target_compile_definitions(Atlas PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Atlas
        PRIVATE atlas_dsp juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    # JUCE puts the bundle at <build>/Atlas_artefacts/<config>/VST3/ATLAS.vst3
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Atlas_artefacts/$<CONFIG>/VST3/ATLAS.vst3"
            DESTINATION vst3 COMPONENT Runtime)
endif()

# --- Installer (CPack) ----------------------------------------------------------------------
# Windows: NSIS setup .exe (Start-menu entry, uninstaller, PATH-free) + portable ZIP.
# Linux:   TGZ archive.
# Build it with:  cmake --build build --config Release --target package
if(NOT WIN32)
    install(DIRECTORY src/ DESTINATION include/atlas COMPONENT Development FILES_MATCHING PATTERN "*.h")
endif()
install(FILES docs/STATUS.md DESTINATION share/doc/atlas COMPONENT Runtime)

set(CPACK_PACKAGE_NAME "ATLAS")
set(CPACK_PACKAGE_VENDOR "ATLAS")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ATLAS - Kick Designer, Tuner & Layering Plugin")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ATLAS")
set(CPACK_PACKAGE_FILE_NAME "ATLAS-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-setup")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/docs/STATUS.md")
set(CPACK_THREADS 0)
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "ATLAS ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "ATLAS")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_DEFINES "RequestExecutionLevel admin")
    # FL Studio (and every other host) scans the standard VST3 folder. Copy the bundle there.
    # NOTE: CPack writes these values verbatim inside "..." in CPackConfig.cmake, so backslashes
    # are doubled (\\) and quotes escaped (\") here. NSIS finally sees single backslashes.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
SetRegView 64
CreateDirectory $COMMONFILES64\\VST3
RMDir /r $COMMONFILES64\\VST3\\ATLAS.vst3
nsExec::Exec 'xcopy /E /I /Y /Q \"$INSTDIR\\vst3\\ATLAS.vst3\" \"$COMMONFILES64\\VST3\\ATLAS.vst3\"'
Pop $0
]=])
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
SetRegView 64
RMDir /r $COMMONFILES64\\VST3\\ATLAS.vst3
]=])
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
