# Falcon temporary installer. It reuses the runtime's drivers, startup code,
# linker script and toolchain; only App/ holds installer-specific sources.
cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../runtime" CACHE PATH "Falcon runtime sources")
set(FALCON_SOURCE_DATE_EPOCH "1792800000" CACHE STRING "Reproducible build epoch")
set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;SOURCE_DATE_EPOCH=${FALCON_SOURCE_DATE_EPOCH}")
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(FALCON_INSTALLER)
enable_language(C ASM)

# One version for both images: read it from the runtime presets.
file(READ "${RUNTIME_DIR}/CMakePresets.json" RUNTIME_PRESETS)
string(JSON VERSION_STRING_2 GET "${RUNTIME_PRESETS}" configurePresets 0 cacheVariables VERSION_STRING_2)
# Slot name written in the header of the installed firmware: Falcon 1K or
# Falcon 4K (tools/build-local.sh passes the edition of the payload).
set(INSTALL_EDITION "Falcon 4K" CACHE STRING "Edition of the installed firmware")

if(TARGET)
    set(EXE_NAME ${TARGET})
else()
    set(EXE_NAME "installer")
endif()
add_executable(${EXE_NAME})

add_subdirectory("${RUNTIME_DIR}/App/Drivers" Drivers)
add_subdirectory("${RUNTIME_DIR}/App/Core" Core)
add_subdirectory(App)
target_link_libraries(${EXE_NAME} Core App)
target_link_libraries(${EXE_NAME} ${TOOLCHAIN_LINK_LIBRARIES})

target_compile_options(${EXE_NAME} PRIVATE $<$<COMPILE_LANGUAGE:C>:-flto=auto>)
target_link_options(${EXE_NAME} PRIVATE -flto=auto -Os)
set_source_files_properties(
    "${RUNTIME_DIR}/App/driver/mb_flash.c"
    "${RUNTIME_DIR}/App/driver/py25q16.c"
    PROPERTIES COMPILE_OPTIONS "-fno-lto"
)
target_link_options(${EXE_NAME} PRIVATE "-Wl,-Map=${EXE_NAME}.map" -Wl,--no-warn-rwx-segment
                    "-Wl,--defsym=__min_heap_size=0")

# Same hard gate as the runtime: the restore stub must stay self-contained.
add_custom_command(
    TARGET ${EXE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DOBJDUMP=${CMAKE_OBJDUMP}"
            "-DELF=$<TARGET_FILE:${EXE_NAME}>"
            "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.map"
            "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${RUNTIME_DIR}/cmake/check_mb_ramfunc.cmake"
    COMMENT "Checking multiboot RAM stub isolation"
    VERBATIM
)
add_custom_command(
    TARGET ${EXE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${EXE_NAME}.elf ${EXE_NAME}.bin
    COMMENT "Generating .bin file"
)
