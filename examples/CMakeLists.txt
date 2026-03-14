# =============================================================================
# GSST — Examples
# =============================================================================

add_executable(gsst_example_basic basic_usage.cu)
target_link_libraries(gsst_example_basic PRIVATE gsst::gsst)

add_executable(gsst_example_file file_compress.cu)
target_link_libraries(gsst_example_file PRIVATE gsst::gsst)
