# Cross build for Windows with llvm-mingw (tools/build_windows.sh).
#   MC3_WIN_ARCH      x86_64 (default) or aarch64
#   MC3_LLVM_MINGW    the llvm-mingw folder (bin/<arch>-w64-mingw32-clang)
#   MC3_WIN_SYSROOT   the MSYS2 libraries (tools/win_deps.py)
set(CMAKE_SYSTEM_NAME Windows)
set(_arch "$ENV{MC3_WIN_ARCH}")
if(NOT _arch)
    set(_arch x86_64)
endif()
set(CMAKE_SYSTEM_PROCESSOR ${_arch})
set(_triple ${_arch}-w64-mingw32)

set(CMAKE_C_COMPILER   "$ENV{MC3_LLVM_MINGW}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "$ENV{MC3_LLVM_MINGW}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER  "$ENV{MC3_LLVM_MINGW}/bin/${_triple}-windres")
set(CMAKE_AR           "$ENV{MC3_LLVM_MINGW}/bin/llvm-ar")
set(CMAKE_RANLIB       "$ENV{MC3_LLVM_MINGW}/bin/llvm-ranlib")

set(CMAKE_FIND_ROOT_PATH "$ENV{MC3_WIN_SYSROOT}" "$ENV{MC3_LLVM_MINGW}/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config: the host's, reading only the Windows .pc files.
set(ENV{PKG_CONFIG_LIBDIR} "$ENV{MC3_WIN_SYSROOT}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
