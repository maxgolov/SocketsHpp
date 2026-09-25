# Cross-compile for 64-bit Windows with MinGW-w64 (posix threads), e.g. on Ubuntu:
#   sudo apt-get install g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix
#   cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake
# Tests can be run under Wine by also passing -DCMAKE_CROSSCOMPILING_EMULATOR=<path to wine64>.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_mingw_prefix x86_64-w64-mingw32)
find_program(CMAKE_C_COMPILER NAMES ${_mingw_prefix}-gcc-posix ${_mingw_prefix}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${_mingw_prefix}-g++-posix ${_mingw_prefix}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES ${_mingw_prefix}-windres)

list(APPEND CMAKE_FIND_ROOT_PATH /usr/${_mingw_prefix})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static runtime so the test executables run without MinGW DLLs on PATH.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
