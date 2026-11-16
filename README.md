# rtf-atto

rtf-atto is an RTF reader and writer in C++17.
It targets clinical tables made by SAS and also reads general RTF.
A WebAssembly build and a TypeScript renderer will come later.

## Build and test

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release -j 8
    ctest --test-dir build -C Release --output-on-failure

Status: plan 1 (the reader) in progress; see docs/plans/.

## Licence

Apache-2.0. See LICENSE.
