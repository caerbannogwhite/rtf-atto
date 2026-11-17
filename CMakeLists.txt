cmake_minimum_required(VERSION 3.20)
project(rtf-atto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(RTF_IS_TOP_LEVEL ON)
else()
  set(RTF_IS_TOP_LEVEL OFF)
endif()

option(RTF_BUILD_TESTS "Build the tests" ${RTF_IS_TOP_LEVEL})
option(RTF_BUILD_TOOLS "Build the command line tools" ${RTF_IS_TOP_LEVEL})
option(RTF_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(RTF_FUZZ "Build the libFuzzer target (needs Clang)" OFF)

if(RTF_SANITIZE AND MSVC)
  message(WARNING "RTF_SANITIZE does nothing with the MSVC compiler; use clang++ (see the README)")
endif()
# On Windows, Clang's sanitizer and fuzzer runtimes are built against the
# static release C runtime, so these builds select it for every configuration.
if(WIN32 AND (RTF_SANITIZE OR RTF_FUZZ))
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# Flags shared by the library, the tools and the tests.
add_library(rtf_atto_flags INTERFACE)
if(MSVC)
  target_compile_options(rtf_atto_flags INTERFACE /W4 /WX /permissive-)
else()
  target_compile_options(rtf_atto_flags INTERFACE
    -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror)
  if(RTF_SANITIZE)
    target_compile_options(rtf_atto_flags INTERFACE
      -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(rtf_atto_flags INTERFACE
      -fsanitize=address,undefined -fno-sanitize-recover=all)
  endif()
endif()

add_library(rtf_atto STATIC src/version.cpp src/tokenizer.cpp src/encodings.cpp
  src/builder.cpp src/reader.cpp src/json.cpp src/text.cpp src/tables.cpp)
target_include_directories(rtf_atto PUBLIC include PRIVATE src)
target_link_libraries(rtf_atto PUBLIC rtf_atto_flags)

if(RTF_BUILD_TOOLS)
  add_executable(rtf2json tools/rtf2json.cpp)
  target_link_libraries(rtf2json PRIVATE rtf_atto rtf_atto_flags)
endif()

if(RTF_BUILD_TESTS)
  enable_testing()

  # rtf_add_test(<name> [args...]) builds test/cpp/test_<name>.cpp and
  # registers a CTest test called <name>. The args become its command line.
  function(rtf_add_test name)
    add_executable(test_${name} test/cpp/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE rtf_atto)
    target_include_directories(test_${name} PRIVATE test/cpp ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND test_${name} ${ARGN})
  endfunction()

  rtf_add_test(model)
  rtf_add_test(tokenizer)
  rtf_add_test(encodings)
  rtf_add_test(reader_text)
  rtf_add_test(reader_tables)
  rtf_add_test(reader_structure)
  rtf_add_test(json)
  rtf_add_test(tables)
  rtf_add_test(goldens ${CMAKE_SOURCE_DIR}/test/fixtures)
  rtf_add_test(mutation ${CMAKE_SOURCE_DIR}/test/fixtures)
  set_tests_properties(mutation PROPERTIES TIMEOUT 900)
  rtf_add_test(large)
  set_tests_properties(large PROPERTIES TIMEOUT 300)

  # The text projection of the C++ reader is compared with an independent
  # extractor written in Python (test/tools/rtf_text.py). The test needs
  # the rtf2json tool.
  if(RTF_BUILD_TOOLS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
      add_test(NAME text_coverage
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/tools/check_text_coverage.py
                $<TARGET_FILE:rtf2json> ${CMAKE_SOURCE_DIR}/test/fixtures)
    else()
      message(WARNING "Python 3 not found; the text_coverage test is not registered")
    endif()
  endif()
endif()

# The libFuzzer target, for long runs on a developer's machine. CI does not build it.
if(RTF_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "RTF_FUZZ needs Clang")
  endif()
  # The library must be instrumented too. Otherwise libFuzzer gets no
  # coverage from the parser and AddressSanitizer does not check it.
  target_compile_options(rtf_atto_flags INTERFACE -fsanitize=fuzzer-no-link,address)
  target_link_options(rtf_atto_flags INTERFACE -fsanitize=fuzzer-no-link,address)
  if(WIN32)
    # The prebuilt libFuzzer runtime was compiled without the MSVC STL's
    # AddressSanitizer container annotations, and the linker folds its
    # std::string code with ours. Mixed annotation state produced a false
    # container-overflow report on 2026-11-17, so this build turns the
    # annotations off. The plain sanitizer build (RTF_SANITIZE) keeps them.
    target_compile_definitions(rtf_atto_flags INTERFACE _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)
  endif()
  add_executable(fuzz_parse test/fuzz/fuzz_parse.cpp)
  target_link_libraries(fuzz_parse PRIVATE rtf_atto)
  target_compile_options(fuzz_parse PRIVATE -fsanitize=fuzzer,address)
  target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer,address)
endif()
