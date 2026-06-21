# SPDX-License-Identifier: GPL-2.0
cmake_minimum_required(VERSION 3.16)
project(driver_psvr2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Locate the in-tree references and the userspace library. Overridable so the
# driver can also build against an installed OpenVR / libpsvr2.
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(OPENVR_DIR "${REPO_ROOT}/references/openvr" CACHE PATH "OpenVR SDK root")
set(OPENVR_SAMPLE_UTILS "${OPENVR_DIR}/samples/drivers/utils")
set(LIBPSVR2_DIR "${REPO_ROOT}/userspace/lib" CACHE PATH "libpsvr2 source dir")

# driverlog is a tiny logging wrapper shipped with the OpenVR samples.
add_library(driver_psvr2 MODULE
    src/hmd_driver_factory.cpp
    src/device_provider.cpp
    src/hmd_device_driver.cpp
    src/pose_source.cpp
    "${OPENVR_SAMPLE_UTILS}/driverlog/driverlog.cpp"
)

target_include_directories(driver_psvr2 PRIVATE
    src
    "${OPENVR_DIR}/headers"
    "${OPENVR_SAMPLE_UTILS}/driverlog"
    "${OPENVR_SAMPLE_UTILS}/vrmath"
    "${LIBPSVR2_DIR}"
)

# Link libpsvr2. Prefer the in-tree static archive so the driver is
# self-contained for SteamVR (no runtime .so path to chase); fall back to
# pkg-config if installed. Name the archive file directly so CMake doesn't
# resolve the shared libpsvr2.so instead.
find_library(LIBPSVR2_STATIC NAMES libpsvr2.a PATHS "${LIBPSVR2_DIR}" NO_DEFAULT_PATH)
if(LIBPSVR2_STATIC)
    target_link_libraries(driver_psvr2 PRIVATE "${LIBPSVR2_STATIC}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PSVR2 REQUIRED libpsvr2)
    target_include_directories(driver_psvr2 PRIVATE ${PSVR2_INCLUDE_DIRS})
    target_link_libraries(driver_psvr2 PRIVATE ${PSVR2_LIBRARIES})
endif()

find_package(Threads REQUIRED)
target_link_libraries(driver_psvr2 PRIVATE Threads::Threads)

# SteamVR expects the driver as <driver>/bin/linux64/driver_<name>.so with no
# "lib" prefix. Lay the whole package out under the build dir so it can be
# symlinked straight into SteamVR's drivers/ directory.
set(PKG_DIR "${CMAKE_BINARY_DIR}/psvr2")
set_target_properties(driver_psvr2 PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${PKG_DIR}/bin/linux64"
)

# Stage the resources (manifest + settings) alongside the binary. Use a separate
# ALL target (not a POST_BUILD on the .so) so resource-only edits — e.g. the
# manifest or default.vrsettings — re-stage every build even when the shared
# object itself does not relink.
add_custom_target(stage_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PKG_DIR}"
    COMMENT "Staging SteamVR driver resources at ${PKG_DIR}"
    VERBATIM
)
add_dependencies(stage_resources driver_psvr2)
