# =============================================================================
# GSST — Tests
# =============================================================================

include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper: test data generator
# ---------------------------------------------------------------------------
add_library(gsst_test_helpers INTERFACE)
target_include_directories(gsst_test_helpers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

# Format header serialization tests
add_executable(gsst_format_test format_test.cpp)
target_link_libraries(gsst_format_test PRIVATE gsst::gsst GTest::gtest_main)
gtest_discover_tests(gsst_format_test)

# Symbol table construction and serialization tests
add_executable(gsst_symbol_table_test symbol_table_test.cpp)
target_link_libraries(gsst_symbol_table_test PRIVATE gsst::gsst GTest::gtest_main)
gtest_discover_tests(gsst_symbol_table_test)

# Roundtrip compress → decompress tests
add_executable(gsst_roundtrip_test roundtrip_test.cpp)
target_link_libraries(gsst_roundtrip_test PRIVATE gsst::gsst GTest::gtest_main)
gtest_discover_tests(gsst_roundtrip_test)

# C API tests
add_executable(gsst_c_api_test c_api_test.cpp)
target_link_libraries(gsst_c_api_test PRIVATE gsst::gsst GTest::gtest_main)
gtest_discover_tests(gsst_c_api_test)

# Codec edge-case and stress tests
add_executable(gsst_codec_test codec_test.cpp)
target_link_libraries(gsst_codec_test PRIVATE gsst::gsst GTest::gtest_main)
gtest_discover_tests(gsst_codec_test)
