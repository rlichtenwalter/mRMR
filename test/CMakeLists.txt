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

add_executable(bench_view_tiled bench_view_tiled.cpp)
target_link_libraries(bench_view_tiled PRIVATE mrmr::mrmr Catch2::Catch2WithMain)
target_compile_options(bench_view_tiled PRIVATE -O3)

add_executable(bench_view_1m bench_view_1m.cpp)
target_link_libraries(bench_view_1m PRIVATE mrmr::mrmr Catch2::Catch2WithMain)
target_compile_options(bench_view_1m PRIVATE -O3)

add_executable(bench_continuous bench_continuous.cpp)
target_link_libraries(bench_continuous PRIVATE mrmr::mrmr Catch2::Catch2WithMain)
target_compile_options(bench_continuous PRIVATE -O3)

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

# --- New CLI feature tests ---

add_test(NAME cli_info COMMAND ${MRMR_CLI} --info -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_info PROPERTIES PASS_REGULAR_EXPRESSION "Instances:.*5")

add_test(NAME cli_write_data COMMAND ${MRMR_CLI} --write-data -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_write_data PROPERTIES PASS_REGULAR_EXPRESSION "class\tbinary_feature")

add_test(NAME cli_missing_error COMMAND ${MRMR_CLI} --missing=error -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_missing_error PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

add_test(NAME cli_missing_bad COMMAND ${MRMR_CLI} --missing=invalid "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_missing_bad PROPERTIES WILL_FAIL true)

# --missing=error with actual missing data should fail
add_test(NAME cli_missing_error_with_nan COMMAND ${MRMR_CLI} --missing=error -d truncate "${TEST_DATA_DIR}/missing_example.tsv")
set_tests_properties(cli_missing_error_with_nan PROPERTIES WILL_FAIL true)

# --missing=impute-mode with missing data should succeed
add_test(NAME cli_missing_impute_mode COMMAND ${MRMR_CLI} --missing=impute-mode -d truncate -v quiet "${TEST_DATA_DIR}/missing_example.tsv")
set_tests_properties(cli_missing_impute_mode PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

# --missing=impute-median with missing data
add_test(NAME cli_missing_impute_median COMMAND ${MRMR_CLI} --missing=impute-median -d truncate -v quiet "${TEST_DATA_DIR}/missing_example.tsv")
set_tests_properties(cli_missing_impute_median PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

# --missing=impute-mean with missing data
add_test(NAME cli_missing_impute_mean COMMAND ${MRMR_CLI} --missing=impute-mean -d truncate -v quiet "${TEST_DATA_DIR}/missing_example.tsv")
set_tests_properties(cli_missing_impute_mean PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

# --missing=pairwise with missing data
add_test(NAME cli_missing_pairwise COMMAND ${MRMR_CLI} --missing=pairwise -d truncate -v quiet "${TEST_DATA_DIR}/missing_example.tsv")
set_tests_properties(cli_missing_pairwise PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

add_test(NAME cli_ensemble_exhaustive COMMAND ${MRMR_CLI} -e exhaustive -n 2 -k 2 -v quiet -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_ensemble_exhaustive PROPERTIES PASS_REGULAR_EXPRESSION "Consensus Ranking")

add_test(NAME cli_ensemble_bootstrap COMMAND ${MRMR_CLI} -e bootstrap -n 3 -k 2 -s 42 -v quiet -d truncate "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_ensemble_bootstrap PROPERTIES PASS_REGULAR_EXPRESSION "Consensus Ranking")

# E2E: class at rank 0, first feature at rank 1
add_test(NAME cli_e2e_output COMMAND ${MRMR_CLI} -d truncate -v quiet "${TEST_DATA_DIR}/example.tsv")
set_tests_properties(cli_e2e_output PROPERTIES PASS_REGULAR_EXPRESSION "0\t0\tclass.*\n1\t")

if(MRMR_CONTINUOUS)
    add_test(NAME cli_method_continuous COMMAND ${MRMR_CLI} --method=continuous -v quiet "${TEST_DATA_DIR}/mixed_example.tsv")
    set_tests_properties(cli_method_continuous PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

    add_test(NAME cli_info_continuous COMMAND ${MRMR_CLI} --info --method=continuous "${TEST_DATA_DIR}/mixed_example.tsv")
    set_tests_properties(cli_info_continuous PROPERTIES PASS_REGULAR_EXPRESSION "continuous")

    add_test(NAME cli_ksg_k COMMAND ${MRMR_CLI} --method=continuous --ksg-k=3 -v quiet "${TEST_DATA_DIR}/mixed_example.tsv")
    set_tests_properties(cli_ksg_k PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")

    add_test(NAME cli_conflict_disc_cont COMMAND ${MRMR_CLI} --method=continuous --discretize=round "${TEST_DATA_DIR}/mixed_example.tsv")
    set_tests_properties(cli_conflict_disc_cont PROPERTIES WILL_FAIL true)

    add_test(NAME cli_bare_names_cont COMMAND ${MRMR_CLI} --method=continuous -v quiet "${TEST_DATA_DIR}/example.tsv")
    set_tests_properties(cli_bare_names_cont PROPERTIES PASS_REGULAR_EXPRESSION "Rank\tIndex\tName")
endif()
