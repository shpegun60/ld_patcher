#!/usr/bin/env bash
set -euo pipefail

ROOT=${LDPATCHER_WORKSPACE_ROOT:-/c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32}
SRC=${LDPATCHER_SOURCE_ROOT:-$ROOT/gnu-tools-for-stm32-13.3.rel1.20250523-0900}
BLD=${LDPATCHER_BUILD_DIR:-$ROOT/manual_build_fallback/_build-ld-st-fallback}
INS=${LDPATCHER_INSTALL_DIR:-$ROOT/manual_build_fallback/_install-ld-st-fallback}
JOBS=${JOBS:-$(nproc)}

export PATH=/mingw64/bin:/usr/bin

rm -rf "$BLD" "$INS"
mkdir -p "$BLD" "$INS"

cd "$BLD"
export CFLAGS="-I$SRC/src/liblongpath-win32/include"
export CPPFLAGS="-I$SRC/src/liblongpath-win32/include"

"$SRC/src/binutils/configure" \
  --prefix="$INS" \
  --build=x86_64-w64-mingw32 \
  --host=x86_64-w64-mingw32 \
  --target=arm-none-eabi \
  --disable-gdb --disable-sim \
  --disable-nls \
  --enable-plugins \
  --with-sysroot="$INS/arm-none-eabi" \
  --with-pkgversion="GNU Tools for STM32 fallback" \
  --disable-werror

make -j"$JOBS" MAKEINFO=true all-ld
make MAKEINFO=true install-ld

