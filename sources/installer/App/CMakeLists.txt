add_library(App INTERFACE)
# Installer sources first, then the runtime's shared headers and drivers.
target_include_directories(App INTERFACE . "${RUNTIME_DIR}/App")
target_link_libraries(App INTERFACE PY32F071_Driver)
target_compile_definitions(App INTERFACE
    FALCON_INSTALLER
    ENABLE_4096
    ENABLE_FEAT_F4HWN_MULTIBOOT
    ENABLE_FEAT_F4HWN_MULTIBOOT_OVERLAY
    EDITION_STRING="Falcon"
    VERSION_STRING_2="${VERSION_STRING_2}"
    INSTALL_EDITION="${INSTALL_EDITION}"
)
target_sources(App INTERFACE
    main.c
    install_payload.c
    pack_inflate.c
    scheduler.c
    "${RUNTIME_DIR}/App/board.c"
    "${RUNTIME_DIR}/App/driver/gpio.c"
    "${RUNTIME_DIR}/App/driver/mb_flash.c"
    "${RUNTIME_DIR}/App/driver/py25q16.c"
    "${RUNTIME_DIR}/App/driver/st7565.c"
    "${RUNTIME_DIR}/App/driver/system.c"
    "${RUNTIME_DIR}/App/driver/systick.c"
)
