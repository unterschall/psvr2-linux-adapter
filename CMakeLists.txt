cmake_minimum_required(VERSION 3.10)
project(psvr2-linux-adapter
        VERSION 0.1.0
        DESCRIPTION "Linux driver for Sony PSVR2 PC adapter"
        LANGUAGES C)

# Options
option(ENABLE_KERNEL_MODULE "Build the kernel module" ON)
option(ENABLE_USERSPACE "Build userspace tools and libraries" OFF)
option(ENABLE_DKMS "Configure for DKMS installation" OFF)
option(ENABLE_TESTS "Build tests" OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()

# Add our custom cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake")

# Include helper modules
include(DistroDetect)
include(KernelModule)
include(PackageRequirements)

# Display project information
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Description: ${PROJECT_DESCRIPTION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Detected Linux distribution: ${DETECTED_DISTRO}")
message(STATUS "Detected distribution family: ${DETECTED_DISTRO_FAMILY}")
message(STATUS "Kernel version: ${KERNEL_VERSION}")
message(STATUS "Kernel headers: ${KERNEL_HEADERS_PATH}")

# Configure the kernel module build if enabled
if(ENABLE_KERNEL_MODULE)
    add_subdirectory(kernel)
endif()

# Configure userspace components if enabled
if(ENABLE_USERSPACE)
    add_subdirectory(userspace)
endif()

# Configure tests if enabled
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install udev rules
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/99-psvr2.rules
        DESTINATION /etc/udev/rules.d
        COMPONENT udev_rules)

# Create uninstall target
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    @ONLY)
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

# Generate installation package if requested
include(InstallRequirements)
