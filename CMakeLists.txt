cmake_minimum_required(VERSION 3.15)

project(fastcsa)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(FASTCSA_IS_MAIN_PROJECT TRUE)
else()
  set(FASTCSA_IS_MAIN_PROJECT FALSE)
endif()

if(FASTCSA_IS_MAIN_PROJECT)
  add_subdirectory(_deps/argparse-src)
endif()

set(YAEF_OPTS_FORCE_NO_AVX512 ON CACHE BOOL "force disable AVX512" FORCE)
add_subdirectory(_deps/yaef-src)

option(FASTCSA_ENABLE_TESTS "enable fastcsa tests" ${FASTCSA_IS_MAIN_PROJECT})

add_subdirectory(_deps/libsais-src)

add_library(fastcsa "${CMAKE_CURRENT_SOURCE_DIR}/source/fastcsa.cpp")
add_library(fastcsa::fastcsa ALIAS fastcsa)
target_link_libraries(fastcsa PUBLIC yaef::yaef PRIVATE libsais)
target_include_directories(fastcsa PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(fastcsa PRIVATE "${DIVSUFSORT64_INCLDUE_DIR}")
target_compile_options(fastcsa PUBLIC -Wno-sign-compare -mavx2)

if(FASTCSA_IS_MAIN_PROJECT)
  add_executable(benchmark "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/benchmark.cpp")
  target_link_libraries(benchmark PRIVATE argparse::argparse fastcsa::fastcsa)
endif()

if(FASTCSA_IS_MAIN_PROJECT AND FASTCSA_ENABLE_TESTS)
  enable_testing()
  if(NOT TARGET Catch2::Catch2WithMain)
    add_subdirectory(_deps/yaef-src/deps/Catch2 _deps/catch2-build EXCLUDE_FROM_ALL)
  endif()
  add_subdirectory(tests)
endif()
