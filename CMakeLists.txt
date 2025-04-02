cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0135 NEW)

separate_arguments(A1C_C_FLAGS NATIVE_COMMAND "${A1C_C_FLAGS}")
option(STRICT "Enable strict compiler flags for C" ON)

project(a1-cbor LANGUAGES C CXX)

add_library(a1-cbor a1cbor.c)
if (${STRICT})
    target_compile_options(
        a1-cbor
        PRIVATE
            -g
            -Wall
            -Wextra
            -pedantic
            -Werror
            -Wconversion
            -Wcast-qual
            -Wcast-align
            -Wformat=2
            -Wundef
            -Wshadow
            -Wvla
            -Wpointer-arith
            -Wstrict-aliasing=1
            -Wfloat-equal
            -Wunused-function
            -Wswitch-enum
    )
endif()
target_compile_options(a1-cbor PRIVATE ${A1C_C_FLAGS})
set_property(TARGET a1-cbor PROPERTY C_STANDARD 11)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/6910c9d9165801d8827d628cb72eb7ea9dd538c5.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(
    a1-cbor-tests
    tests/test_a1cbor.cpp
)
target_link_libraries(
  a1-cbor-tests
  a1-cbor
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(a1-cbor-tests)