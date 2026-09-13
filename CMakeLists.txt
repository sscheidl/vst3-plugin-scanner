cmake_minimum_required(VERSION 3.25)

project(Vst3PluginScannerPoc VERSION 2.4.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "This proof of concept currently supports Windows only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This proof of concept supports x64 builds only.")
endif()

set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH
    "Path to a recursive checkout of the official Steinberg VST3 SDK")
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "Use the static MSVC runtime" FORCE)

if(NOT EXISTS "${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.h" OR
   NOT EXISTS "${VST3_SDK_ROOT}/pluginterfaces/base/ipluginbase.h")
    message(FATAL_ERROR
        "The Steinberg VST3 SDK is missing. Run: git submodule update --init --recursive")
endif()

# Build only the official SDK libraries needed by the metadata probe. Adding the
# complete SDK tree would also generate unrelated examples and utilities.
list(APPEND CMAKE_MODULE_PATH "${VST3_SDK_ROOT}/cmake/modules")
include(SMTG_VST3_SDK)

set(SDK_ROOT "${VST3_SDK_ROOT}")
set(SDK_IDE_LIBS_FOLDER FOLDER "Libraries/Steinberg VST3 SDK")
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_hosting_target()

add_library(Vst3ProbeProtocol STATIC
    probe/src/JsonProtocol.cpp
)
target_include_directories(Vst3ProbeProtocol PUBLIC probe/include)
target_compile_definitions(Vst3ProbeProtocol PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(Vst3ProbeProtocol PRIVATE /W4 /permissive-)

add_library(Vst3InventoryCore STATIC
    inventory/src/InventoryModel.cpp
)
target_include_directories(Vst3InventoryCore PUBLIC inventory/include)
target_include_directories(Vst3InventoryCore PRIVATE
    "${VST3_SDK_ROOT}/vstgui4/vstgui/thirdparty/rapidjson/include"
)
target_compile_definitions(Vst3InventoryCore PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(Vst3InventoryCore PRIVATE /W4 /permissive-)
target_link_libraries(Vst3InventoryCore PUBLIC Vst3ProbeProtocol)

add_executable(Vst3MetadataProbe
    probe/Vst3MetadataProbe.rc
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp
    probe/src/Vst3FactoryReader.cpp
    probe/src/main.cpp
)
target_include_directories(Vst3MetadataProbe PRIVATE probe/include)
target_compile_definitions(Vst3MetadataProbe PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(Vst3MetadataProbe PRIVATE /W4 /permissive-)
target_link_libraries(Vst3MetadataProbe PRIVATE Vst3ProbeProtocol sdk_hosting)
set_target_properties(Vst3MetadataProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(Vst3ProbeGui WIN32
    gui/Vst3ProbeGui.manifest
    gui/Vst3ProbeGui.rc
    gui/src/main.cpp
)
target_compile_definitions(Vst3ProbeGui PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(Vst3ProbeGui PRIVATE /W4 /permissive- /utf-8)
target_include_directories(Vst3ProbeGui PRIVATE inventory/include probe/include)
target_link_libraries(Vst3ProbeGui PRIVATE Vst3InventoryCore Comctl32 Ole32 Shell32)
add_dependencies(Vst3ProbeGui Vst3MetadataProbe)
set_target_properties(Vst3ProbeGui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(BUILD_TESTING)
    add_executable(Vst3ProbeTests
        probe/tests/ProbeTests.cpp
    )
    target_include_directories(Vst3ProbeTests PRIVATE probe/include)
    target_compile_definitions(Vst3ProbeTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(Vst3ProbeTests PRIVATE /W4 /permissive-)
    target_link_libraries(Vst3ProbeTests PRIVATE Vst3ProbeProtocol sdk_hosting)
    set_target_properties(Vst3ProbeTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(NAME Vst3ProbeTests COMMAND Vst3ProbeTests)

    add_executable(Vst3InventoryTests
        inventory/tests/InventoryTests.cpp
    )
    target_include_directories(Vst3InventoryTests PRIVATE inventory/include probe/include)
    target_compile_definitions(Vst3InventoryTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(Vst3InventoryTests PRIVATE /W4 /permissive-)
    target_link_libraries(Vst3InventoryTests PRIVATE Vst3InventoryCore)
    set_target_properties(Vst3InventoryTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(NAME Vst3InventoryTests COMMAND Vst3InventoryTests)
endif()
