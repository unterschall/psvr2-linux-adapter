# Kernel module build configuration

# Ensure kernel build system is available
if(NOT KBUILD_FOUND)
    message(FATAL_ERROR "Kernel build system not found. Please install the appropriate kernel headers package.")
endif()

# Define module sources
set(MODULE_SOURCES
    "module/psvr2_adapter_main.c"
    "module/psvr2_display.c"
    "module/psvr2_input.c"
    "module/psvr2_hid.c"
)

# Use our kernel module build function to create the module
add_kernel_module(psvr2_adapter
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    SOURCES ${MODULE_SOURCES}
)

# Create a symlink to the module header directory
add_custom_command(
    TARGET psvr2_adapter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/include"
    COMMENT "Creating symlink to header files for module build"
)

# Configure Kbuild makefile with proper paths
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/module/Makefile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Makefile"
    @ONLY
)

# Install targets
if(NOT ENABLE_DKMS)
    # Install kernel module
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/psvr2_adapter.ko"
            DESTINATION "/lib/modules/${KERNEL_RELEASE}/extra"
            COMPONENT kernel_module)
            
    # Add post-install command to run depmod
    install(CODE "execute_process(COMMAND depmod -a ${KERNEL_RELEASE})")
endif()
