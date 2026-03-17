include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# --- Unit tests ---
add_executable(test_mrmr test_mrmr.cpp)
target_link_libraries(test_mrmr PRIVATE mrmr::mrmr Catch2::Catch2WithMain)
catch_discover_tests(test_mrmr)

# --- Benchmarks ---
add_executable(bench_mrmr bench_mrmr.cpp)
target_link_libraries(bench_mrmr PRIVATE mrmr::mrmr Catch2::Catch2WithMain)
target_compile_options(bench_mrmr PRIVATE -O3)

add_executable(bench_view_access bench_view_access.cpp)
target_link_libraries(bench_view_access PRIVATE mrmr::mrmr Catch2::Catch2WithMain)
target_compile_options(bench_view_access PRIVATE -O3)

# --- CLI integration tests ---
set(MRMR_CLI $<TARGET_FILE:mrmr-cli>)
set(TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

add_test(NAME cli_version       COMMAND ${MRMR_CLI} --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^mrmr [0-9]+\\.[0-9]+\\.[0-9]+")

add_test(NAME cli_help          COMMAND ${MRMR_CLI} --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")

add_test(NAME cli_help_short    COMMAND ${MRMR_CLI} -h)
set_tests_properties(cli_help_short PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")

add_test(NAME cli_file_input    COMMAND ${MRMR_CLI} -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_file_input PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

add_test(NAME cli_stdin_input   COMMAND sh -c "cat '${TEST_DATA_DIR}/example.tsv' | ${MRMR_CLI} -d truncate")
set_tests_properties(cli_stdin_input PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

add_test(NAME cli_missing_file  COMMAND ${MRMR_CLI} nonexistent.tsv)
set_tests_properties(cli_missing_file PROPERTIES WILL_FAIL true)

add_test(NAME cli_too_many_args COMMAND ${MRMR_CLI} file1.tsv file2.tsv)
set_tests_properties(cli_too_many_args PROPERTIES WILL_FAIL true)

add_test(NAME cli_bad_delimiter COMMAND ${MRMR_CLI} -t "ab")
set_tests_properties(cli_bad_delimiter PROPERTIES WILL_FAIL true)

add_test(NAME cli_quiet         COMMAND ${MRMR_CLI} -v quiet -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_quiet PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")
