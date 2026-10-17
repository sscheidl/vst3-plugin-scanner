if(NOT CONFIG STREQUAL "Release")
    return()
endif()

set(name "PluginInventoryScanner-${VERSION}")
set(directory "${SOURCE_DIR}/dist/${name}")
file(MAKE_DIRECTORY "${directory}")
file(COPY_FILE "${APP}" "${directory}/PluginInventoryScanner.exe" ONLY_IF_DIFFERENT)
foreach(document README.md STATUS.md THIRD_PARTY_NOTICES.txt)
    file(COPY_FILE "${SOURCE_DIR}/${document}" "${directory}/${document}" ONLY_IF_DIFFERENT)
endforeach()
file(SHA256 "${directory}/PluginInventoryScanner.exe" app_hash)
file(WRITE "${directory}/SHA256SUMS.txt" "${app_hash}  PluginInventoryScanner.exe\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf
    "${name}-windows-x64.zip" --format=zip "${name}"
    WORKING_DIRECTORY "${SOURCE_DIR}/dist"
    COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Release: ${directory}/PluginInventoryScanner.exe")
