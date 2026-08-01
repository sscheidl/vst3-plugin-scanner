cmake_minimum_required(VERSION 3.20)

project(VstPluginScanner LANGUAGES CXX)

include(CTest)

option(VST3_SCANNER_PREPARE_STEINBERG_SDK
       "Validate a Steinberg VST3 SDK checkout for the future out-of-process probe"
       OFF)
set(VST3_SDK_ROOT "" CACHE PATH "Path to a recursive checkout of steinbergmedia/vst3sdk")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(VstPluginScanner WIN32
    src/DuplicateDetector.cpp
    src/MetadataReader.cpp
    src/PluginUserPrefs.cpp
    src/ReportWriter.cpp
    src/ScannerEngine.cpp
    src/StringUtil.cpp
    src/VersionUtil.cpp
    src/Vst3SdkProbeStub.cpp
    src/main.cpp
    VstPluginScanner.rc
)

target_include_directories(VstPluginScanner PRIVATE include)

target_compile_definitions(VstPluginScanner PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(VstPluginScanner PRIVATE
    Comctl32
    Comdlg32
    Version
    Ole32
    Shell32
)

if(VST3_SCANNER_PREPARE_STEINBERG_SDK)
    if(NOT EXISTS "${VST3_SDK_ROOT}/pluginterfaces/vst/ivstcomponent.h" OR
       NOT EXISTS "${VST3_SDK_ROOT}/public.sdk/source/vst/moduleinfo")
        message(FATAL_ERROR
            "VST3_SDK_ROOT must point to a recursive steinbergmedia/vst3sdk checkout")
    endif()
    target_compile_definitions(VstPluginScanner PRIVATE VST3_SCANNER_SDK_LAYOUT_VALIDATED=1)
    message(STATUS "Steinberg VST3 SDK layout validated at: ${VST3_SDK_ROOT}")
endif()

if(BUILD_TESTING)
    add_executable(VersionUtilTests
        tests/VersionUtilTests.cpp
        src/StringUtil.cpp
        src/VersionUtil.cpp
    )
    target_include_directories(VersionUtilTests PRIVATE include)
    target_compile_definitions(VersionUtilTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    add_test(NAME VersionUtilTests COMMAND VersionUtilTests)

    add_executable(MetadataReaderTests
        tests/MetadataReaderTests.cpp
        src/MetadataReader.cpp
        src/StringUtil.cpp
        src/VersionUtil.cpp
        src/Vst3SdkProbeStub.cpp
    )
    target_include_directories(MetadataReaderTests PRIVATE include)
    target_compile_definitions(MetadataReaderTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(MetadataReaderTests PRIVATE Version)
    add_test(NAME MetadataReaderTests
             COMMAND MetadataReaderTests $<TARGET_FILE:VstPluginScanner>)
endif()
