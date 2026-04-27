cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MRMR_VERSION_STRING)
string(STRIP "${MRMR_VERSION_STRING}" MRMR_VERSION_STRING)

project(mrmr
    VERSION ${MRMR_VERSION_STRING}
    LANGUAGES CXX
    DESCRIPTION "Improved mRMR feature selection algorithm"
)

# Always emit compile_commands.json so editor LSPs (clangd) and
# clang-tidy in CI can find compilation database entries without the
# caller having to remember -DCMAKE_EXPORT_COMPILE_COMMANDS=ON.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Header-only library ---
add_library(mrmr INTERFACE)
add_library(mrmr::mrmr ALIAS mrmr)

target_include_directories(mrmr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(mrmr INTERFACE cxx_std_20)

set(MRMR_PUBLIC_HEADERS
    include/mrmr/attribute_information.hpp
    include/mrmr/dataset.hpp
    include/mrmr/detail/delimiter_ctype.hpp
    include/mrmr/matrix.hpp
    include/mrmr/mrmr.hpp
    include/mrmr/typedef.hpp
)

target_sources(mrmr INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${MRMR_PUBLIC_HEADERS}
)

# --- Continuous MI support (optional, fetches kdtree library automatically) ---
option(MRMR_CONTINUOUS "Enable continuous/mixed MI estimators" OFF)
if(MRMR_CONTINUOUS)
    include(FetchContent)
    FetchContent_Declare(
        kdtree
        GIT_REPOSITORY git@git.lichtenwalter.site:rlichten/kdtree.git
        GIT_TAG        develop
    )
    FetchContent_MakeAvailable(kdtree)
    target_link_libraries(mrmr INTERFACE kdtree::kdtree)
    target_compile_definitions(mrmr INTERFACE MRMR_HAS_CONTINUOUS=1)
endif()

# --- CLI tool ---
add_executable(mrmr-cli tools/mrmr-cli.cpp)
target_link_libraries(mrmr-cli PRIVATE mrmr::mrmr)
target_compile_definitions(mrmr-cli PRIVATE MRMR_VERSION="${PROJECT_VERSION}")
set_target_properties(mrmr-cli PROPERTIES OUTPUT_NAME mrmr)

# --- Warning flags ---
# Shared baseline applied to every target that consumes the library
# (CLI, tests, benchmarks). Centralized so that a flag added here lands
# in every consumer build at once and the test/benchmark targets cannot
# silently slide past warnings the CLI would error on.
#
# -Wno-unused-local-typedefs suppresses noise from CRTP/SFINAE-style
# helpers in the public headers.
#
# Beyond -Wall -Wextra -pedantic, the expanded set catches:
#   -Wconversion           lossy implicit numeric conversions
#   -Wsign-conversion      signed<->unsigned implicit conversions
#   -Wshadow               variables shadowing outer-scope names
#   -Wnull-dereference     statically-provable null derefs
#   -Wdouble-promotion     implicit float->double (float literal to
#                          double via "3.14" instead of "3.14f", etc.)
#   -Wimplicit-fallthrough switch case missing [[fallthrough]] attr
# GCC adds two more that Clang does not implement:
#   -Wlogical-op           `a || a` / `a && a` typos
#   -Wduplicated-cond      `if(a) ... else if(a)` typos
set(MRMR_WARNING_FLAGS
    -Wall -Wextra -Werror -pedantic -Wno-unused-local-typedefs
    -Wconversion -Wsign-conversion -Wshadow -Wnull-dereference
    -Wdouble-promotion -Wimplicit-fallthrough
    $<$<CXX_COMPILER_ID:GNU>:-Wlogical-op>
    $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-cond>
)

# Clang's -flto produces LLVM bitcode that the default GNU `ld` cannot
# read ("file format not recognized"). Route Clang link steps through
# `lld`, which speaks bitcode natively. CI installs `lld` alongside
# `clang`. GCC builds keep their default linker.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_link_options(-fuse-ld=lld)
endif()

target_compile_options(mrmr-cli PRIVATE
    ${MRMR_WARNING_FLAGS}
    $<$<CONFIG:Release>:-O3 -fomit-frame-pointer -DNDEBUG>
    $<$<CONFIG:Debug>:-Og -g -fno-omit-frame-pointer>
)
# Enable LTO via the CMake property rather than a raw -flto flag.
# CMake handles the gcc-vs-clang toolchain difference (gcc-ar /
# gcc-ranlib for gcc; llvm-ar / llvm-ranlib for clang) automatically,
# selecting -flto / -flto=thin / -flto=auto as appropriate per compiler.
set_target_properties(mrmr-cli PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

# --- Sanitizers ---
# When -DMRMR_SANITIZE=ON, every consumer of the library (CLI tool,
# tests, benchmarks) is compiled and linked with AddressSanitizer +
# UndefinedBehaviorSanitizer. Guarded on Debug builds so Release
# artifacts are unaffected.
#
# -fno-sanitize-recover=all makes every sanitizer diagnostic a hard
# error; without it UBSan reports and continues, which lets CI regress
# silently.
#
# Flags are applied per-target rather than via the header-only INTERFACE
# library, so build-time sanitizer requirements never leak into the
# installed/exported targets that downstream consumers pick up via
# find_package(mrmr). Each flag is its own generator expression
# (specified, portable) rather than a semicolon-separated list inside a
# single genex (depends on generator-specific split behavior).
option(MRMR_SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer in Debug builds" OFF)
set(MRMR_SANITIZE_FLAGS
    $<$<BOOL:${MRMR_SANITIZE}>:$<$<CONFIG:Debug>:-fsanitize=address,undefined>>
    $<$<BOOL:${MRMR_SANITIZE}>:$<$<CONFIG:Debug>:-fno-sanitize-recover=all>>
    $<$<BOOL:${MRMR_SANITIZE}>:$<$<CONFIG:Debug>:-fno-omit-frame-pointer>>
)
target_compile_options(mrmr-cli PRIVATE ${MRMR_SANITIZE_FLAGS})
target_link_options(mrmr-cli    PRIVATE ${MRMR_SANITIZE_FLAGS})

# --- Tests ---
option(MRMR_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
if(MRMR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# --- Install ---
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS mrmr
    EXPORT mrmrTargets
    FILE_SET HEADERS
)

install(TARGETS mrmr-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(EXPORT mrmrTargets
    FILE mrmrTargets.cmake
    NAMESPACE mrmr::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mrmr
)

configure_package_config_file(
    cmake/mrmrConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/mrmrConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mrmr
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mrmrConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mrmrConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mrmrConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mrmr
)

configure_file(cmake/mrmr.pc.in "${CMAKE_CURRENT_BINARY_DIR}/mrmr.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mrmr.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
