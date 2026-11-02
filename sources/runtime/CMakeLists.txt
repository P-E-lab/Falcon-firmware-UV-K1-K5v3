cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME FALCON)
# Fixed release timestamp makes the supplied firmware and its bound modules
# reproducible. Override with -DFALCON_SOURCE_DATE_EPOCH=... for another build.
set(FALCON_SOURCE_DATE_EPOCH "1792800000" CACHE STRING "Reproducible build epoch")
set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;SOURCE_DATE_EPOCH=${FALCON_SOURCE_DATE_EPOCH}")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

if(ENABLE_FEAT_F4HWN)
    if(NOT AUTHOR_STRING_1)
        set(AUTHOR_STRING_1 "")
    endif()
    if(NOT AUTHOR_STRING_2)
        set(AUTHOR_STRING_2 "FALCON")
    endif()
    if(NOT VERSION_STRING_1)
        set(VERSION_STRING_1 "VXX")
    endif()
    if(NOT VERSION_STRING_2)
        set(VERSION_STRING_2 "VXX")
    endif()
    if(NOT EDITION_STRING)
        set(EDITION_STRING "Custom")
    endif()
    if(NOT AUTHOR_STRING)
        set(AUTHOR_STRING "${AUTHOR_STRING_1}+${AUTHOR_STRING_2}")
    endif()
    if(NOT VERSION_STRING)
        set(VERSION_STRING ${VERSION_STRING_2})
    endif()
else()
    if(NOT AUTHOR_STRING)
        set(AUTHOR_STRING "")
    endif()
    if(NOT VERSION_STRING)
        set(VERSION_STRING "NOGIT")
    endif()
endif()

if(TARGET)
    set(EXE_NAME ${TARGET})
else()
    set(EXE_NAME "firmware")
endif()

message("EXE_NAME = " ${EXE_NAME})

add_executable(${EXE_NAME})

add_subdirectory(App/Drivers)
add_subdirectory(App/Middlewares)
add_subdirectory(App/Core)
add_subdirectory(App)

target_link_libraries(${EXE_NAME} Core App)

target_compile_definitions(${EXE_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)
target_link_libraries(${EXE_NAME} ${TOOLCHAIN_LINK_LIBRARIES})

if(ENABLE_LTO)
    # Falcon v1.0: extended LTO, preserving the NOR/cache and RAM reflash drivers.
    target_compile_options(${EXE_NAME} PRIVATE $<$<COMPILE_LANGUAGE:C>:-flto=auto>)
    target_link_options(${EXE_NAME} PRIVATE -flto=auto -Os)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/App/driver/mb_flash.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/App/driver/py25q16.c"
        PROPERTIES COMPILE_OPTIONS "-fno-lto"
    )
endif()

# Generate map file
target_link_options(${EXE_NAME} PRIVATE "-Wl,-Map=${EXE_NAME}.map")

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${EXE_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${EXE_NAME}.map)

# Force linker to ignore RWX segment warning even if cache exists
target_link_options(${EXE_NAME} PRIVATE -Wl,--no-warn-rwx-segment)

# No malloc/sbrk implementation is linked, so Sonic's reserved 0x7D0-byte heap
# is dead weight. Let the linker drop it when explicitly enabled while keeping
# the historical reservation as the default for custom configurations.
if(ENABLE_NO_HEAP)
    target_link_options(${EXE_NAME} PRIVATE "-Wl,--defsym=__min_heap_size=0")
endif()

# -----------------------------------
#  Post build processing
#

# The restore stub executes while application flash is unavailable. Reject an
# overlay build if the linked stub branches outside its RAM section or if its
# input section refers to any external code/data. This is deliberately a hard
# build gate: either condition could make a restore unrecoverable without DFU.
if(ENABLE_FEAT_F4HWN_MULTIBOOT_OVERLAY)
    add_custom_command(
        TARGET ${EXE_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DOBJDUMP=${CMAKE_OBJDUMP}"
                "-DELF=$<TARGET_FILE:${EXE_NAME}>"
                "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.map"
                "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                -P "${CMAKE_SOURCE_DIR}/cmake/check_mb_ramfunc.cmake"
        COMMENT "Checking multiboot RAM stub isolation"
        VERBATIM
    )
endif()

# Generate .bin file
add_custom_command(
    TARGET ${EXE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -R .fp_resources -R .native_menu -R .native_editor -R .native_rf -R .native_fm -R .native_vfo -R .native_scan -R .native_plot -R .native_lists -R .native_options -R .native_boot -R .native_menukeys -R .native_menuset -R .native_editkeys -R .native_messages -O binary ${EXE_NAME}.elf ${EXE_NAME}.bin
    COMMENT "Generating .bin file"
)

# Generate .hex file
add_custom_command(
    TARGET ${EXE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -R .fp_resources -R .native_menu -R .native_editor -R .native_rf -R .native_fm -R .native_vfo -R .native_scan -R .native_plot -R .native_lists -R .native_options -R .native_boot -R .native_menukeys -R .native_menuset -R .native_editkeys -R .native_messages -O ihex ${EXE_NAME}.elf ${EXE_NAME}.hex
    COMMENT "Generating .hex file"
)

# Pack
# TODO: Incorparate venv?
# if(ENABLE_FEAT_F4HWN)
#     add_custom_command(
#         TARGET ${EXE_NAME}
#         POST_BUILD
#         COMMAND python ${CMAKE_SOURCE_DIR}/fw-pack.py ${TARGET_ELF} ${AUTHOR_STRING_2} ${VERSION_STRING_2} ${EXE_NAME}.packed.bin
#         COMMENT "Generating packed binary"
#     )
# else()
#     add_custom_command(
#         TARGET ${EXE_NAME}
#         POST_BUILD
#         COMMAND python ${CMAKE_SOURCE_DIR}/fw-pack.py ${TARGET_ELF} ${AUTHOR_STRING} ${VERSION_STRING} ${EXE_NAME}.packed.bin
#         COMMENT "Generating packed binary"
#     )
# endif()

