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
option(RTF_BUILD_TOOLS "Build the command line tools" ON)
option(RTF_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(RTF_FUZZ "Build the fuzz targets (not used yet)" OFF)

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
  src/builder.cpp src/reader.cpp)
target_include_directories(rtf_atto PUBLIC include PRIVATE src)
target_link_libraries(rtf_atto PUBLIC rtf_atto_flags)

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
endif()
