# Standalone project that compiles a consumer against an *installed* mrmr.
#
# Deliberately not add_subdirectory()'d from the main build: it must resolve
# mrmr through find_package() and the installed mrmrConfig.cmake, never
# through the in-tree target. Configure it against a staged prefix:
#
#   cmake --install build --prefix "$PWD/stage"
#   cmake -S test/install-test -B build/install-test \
#         -DCMAKE_PREFIX_PATH="$PWD/stage"
#   cmake --build build/install-test
#   ./build/install-test/mrmr_install_consumer
#
# The `install` CI job runs exactly this sequence on every PR.

cmake_minimum_required(VERSION 3.24)

project(mrmr_install_test LANGUAGES CXX)

find_package(mrmr REQUIRED)

add_executable(mrmr_install_consumer consumer.cpp)
target_link_libraries(mrmr_install_consumer PRIVATE mrmr::mrmr)
target_compile_options(mrmr_install_consumer PRIVATE -Wall -Wextra -Werror -pedantic)
