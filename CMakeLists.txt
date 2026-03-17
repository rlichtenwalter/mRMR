cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MRMR_VERSION_STRING)
string(STRIP "${MRMR_VERSION_STRING}" MRMR_VERSION_STRING)

project(mrmr
    VERSION ${MRMR_VERSION_STRING}
    LANGUAGES CXX
    DESCRIPTION "Improved mRMR feature selection algorithm"
)

# --- Header-only library ---
add_library(mrmr INTERFACE)
add_library(mrmr::mrmr ALIAS mrmr)

target_include_directories(mrmr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(mrmr INTERFACE cxx_std_14)

set(MRMR_PUBLIC_HEADERS
    include/mrmr/attribute_information.hpp
    include/mrmr/dataset.hpp
    include/mrmr/delimiter_ctype.hpp
    include/mrmr/matrix.hpp
    include/mrmr/mrmr.hpp
    include/mrmr/typedef.hpp
)

target_sources(mrmr INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${MRMR_PUBLIC_HEADERS}
)

# --- CLI tool ---
add_executable(mrmr-cli tools/mrmr-cli.cpp)
target_link_libraries(mrmr-cli PRIVATE mrmr::mrmr)
target_compile_definitions(mrmr-cli PRIVATE MRMR_VERSION="${PROJECT_VERSION}")
set_target_properties(mrmr-cli PROPERTIES OUTPUT_NAME mrmr)

target_compile_options(mrmr-cli PRIVATE
    -Wall -Wextra -Werror -pedantic -Wno-unused-local-typedefs
    $<$<CONFIG:Release>:-O3 -flto -fomit-frame-pointer>
    $<$<CONFIG:Debug>:-Og -g -fno-omit-frame-pointer>
)

option(MRMR_SANITIZE "Enable AddressSanitizer in Debug builds" OFF)
if(MRMR_SANITIZE)
    target_compile_options(mrmr-cli PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
    target_link_options(mrmr-cli PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
endif()

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
