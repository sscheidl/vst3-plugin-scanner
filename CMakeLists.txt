cmake_minimum_required(VERSION 3.20)

project(VstPluginScanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(VstPluginScanner WIN32
    src/DuplicateDetector.cpp
    src/MetadataReader.cpp
    src/ReportWriter.cpp
    src/ScannerEngine.cpp
    src/StringUtil.cpp
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
