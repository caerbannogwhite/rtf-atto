cmake_minimum_required(VERSION 3.20)
project(rtf-atto-wasm LANGUAGES CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure this directory with emcmake: emcmake cmake -S wasm -B build-wasm")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(RTF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RTF_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. rtf_atto_core)

add_executable(rtf exports.cpp)
target_link_libraries(rtf PRIVATE rtf_atto rtf_atto_flags)
target_link_options(rtf PRIVATE
  -O2
  --no-entry
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createRtfModule
  -sENVIRONMENT=web,worker
  -sALLOW_MEMORY_GROWTH=1
  -sFILESYSTEM=0
  -sSTACK_SIZE=4194304
  -sEXPORTED_FUNCTIONS=_rtf_parse,_rtf_logical_tables,_rtf_free,_rtf_version,_malloc,_free
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32
  -sINCOMING_MODULE_JS_API=wasmBinary,onAbort,print,printErr)
