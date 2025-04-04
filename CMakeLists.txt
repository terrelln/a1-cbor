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
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

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
  nlohmann_json::nlohmann_json
  GTest::gtest_main
)

FetchContent_Declare(
  libcbor
  URL https://github.com/PJK/libcbor/archive/ae000f44e8d2a69e1f72a738f7c0b6b4b7cc4fbf.zip
)
FetchContent_MakeAvailable(libcbor)

add_executable(
    fuzz-decode
    tests/fuzz_decode.cpp
)
target_link_libraries(
    fuzz-decode
    a1-cbor
    libcbor::libcbor
)
target_compile_options(
    fuzz-decode
    PRIVATE
      -fsanitize=fuzzer
)
target_link_options(
    fuzz-decode
    PRIVATE
      -fsanitize=fuzzer
)

add_executable(
    fuzz-json
    tests/fuzz_json.cpp
)
target_link_libraries(
    fuzz-json
    a1-cbor
    nlohmann_json::nlohmann_json
)
target_compile_options(
    fuzz-json
    PRIVATE
      -fsanitize=fuzzer
)
target_link_options(
    fuzz-json
    PRIVATE
      -fsanitize=fuzzer
)

include(GoogleTest)
gtest_discover_tests(a1-cbor-tests)