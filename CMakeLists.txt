cmake_minimum_required(VERSION 3.15)
project(reduct VERSION 2.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE REDUCT_GIT_HASH
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT REDUCT_GIT_HASH)
    set(REDUCT_GIT_HASH "unknown")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/reduct-cli/reduct_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/reduct_version.h"
)

set(REDUCT_SOURCES
    src/atom.c
    src/char.c
    src/closure.c
    src/compile.c
    src/core.c
    src/disasm.c
    src/error.c
    src/eval.c
    src/function.c
    src/gc.c
    src/handle.c
    src/intrinsic.c
    src/item.c
    src/list.c
    src/native.c
    src/parse.c
    src/standard.c
    src/stringify.c
)

add_library(reduct SHARED ${REDUCT_SOURCES})
add_library(reduct_static STATIC ${REDUCT_SOURCES})
if(WIN32)
    set_target_properties(reduct_static PROPERTIES OUTPUT_NAME reduct_static)
else()
    set_target_properties(reduct_static PROPERTIES OUTPUT_NAME reduct)
endif()

target_compile_definitions(reduct PRIVATE REDUCT_BUILD_LIB)
if(WIN32)
    target_compile_definitions(reduct PUBLIC REDUCT_USE_LIB)
endif()

target_include_directories(reduct PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(reduct_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(NOT WIN32)
    target_link_libraries(reduct PRIVATE m)
    target_link_libraries(reduct_static PRIVATE m)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(REDUCT_USE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(REDUCT_USE_SANITIZERS AND NOT WIN32)
    target_compile_options(reduct PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(reduct PRIVATE -fsanitize=address,undefined)
    target_compile_options(reduct_static PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(reduct_static PRIVATE -fsanitize=address,undefined)
endif()

option(REDUCT_USE_FUZZER "Enable libFuzzer instrumentation" OFF)
if(REDUCT_USE_FUZZER AND NOT (WIN32 OR APPLE))
    target_compile_options(reduct_static PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
        -g)
endif()

if(NOT WIN32)
    target_compile_options(reduct PRIVATE -Wall -Wextra -Werror -Wno-uninitialized)
    target_compile_options(reduct_static PRIVATE -Wall -Wextra -Werror -Wno-uninitialized)
endif()

add_subdirectory(tools/reduct-cli)

install(TARGETS reduct reduct_static
    EXPORT ReductTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/reduct/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/reduct)

install(EXPORT ReductTargets
    FILE ReductTargets.cmake
    NAMESPACE Reduct::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Reduct
)

set(Reduct_VERSION ${PROJECT_VERSION})
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ReductConfigVersion.cmake"
    VERSION ${Reduct_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ReductConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ReductConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Reduct
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ReductConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ReductConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Reduct
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/reduct.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/reduct.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/reduct.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

set(CPACK_PACKAGE_NAME "reduct")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Functional, immutable, S-expression based configuration and scripting language")
set(CPACK_PACKAGE_VENDOR "Kai Norberg")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/KaiNorberg/Reduct")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/tests/build/"
    "/.git/"
    "/.vscode/"
    "/.cache/"
    "/temp/"
    "/meta/docs/"
    "\\\\.bak$"
    "\\\\.zst$"
    "/BENCHMARKS.md"
)

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Kai Norberg")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")

set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_URL "https://github.com/KaiNorberg/Reduct")

include(CPack)
