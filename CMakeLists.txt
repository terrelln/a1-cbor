cmake_minimum_required(VERSION 3.20)

separate_arguments(A1C_C_FLAGS NATIVE_COMMAND "${A1C_C_FLAGS}")

project(a1-cbor LANGUAGES C CXX)

add_library(a1-cbor a1cbor.c)
target_compile_definitions(a1-cbor PRIVATE ${A1C_C_FLAGS})
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