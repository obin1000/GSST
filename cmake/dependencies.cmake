# =============================================================================
# External Dependencies
# =============================================================================
include(FetchContent)

# ---------------------------------------------------------------------------
# Google Test (for unit tests)
# ---------------------------------------------------------------------------
if(GSST_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.16.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # Clang 21+ has -Wcharacter-conversion which triggers in GTest's char printers.
    # Disable warnings-as-errors for external code.
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Clang 21+ added -Wcharacter-conversion which fires inside GTest's
    # printer code.  Only inject the suppression when the compiler is new
    # enough to understand it (bundled VS clang-cl may be older).
    foreach(_gtest_target gtest gtest_main gmock gmock_main)
        if(TARGET ${_gtest_target})
            target_compile_options(${_gtest_target} PRIVATE
                $<$<AND:$<CXX_COMPILER_ID:Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,21>>:-Wno-character-conversion>
            )
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Google Benchmark (for benchmarks)
# ---------------------------------------------------------------------------
if(GSST_BUILD_BENCHMARKS)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(BENCHMARK_ENABLE_LIBPFM ON CACHE BOOL "" FORCE)
    endif()

    # Save CMAKE_CXX_FLAGS before Google Benchmark appends MSVC-only /MP.
    set(_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
    FetchContent_MakeAvailable(googlebenchmark)
    set(CMAKE_CXX_FLAGS "${_saved_cxx_flags}" CACHE STRING "" FORCE)
endif()
