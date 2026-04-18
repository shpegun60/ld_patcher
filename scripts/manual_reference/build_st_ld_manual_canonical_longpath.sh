#!/usr/bin/env bash
set -euo pipefail

ROOT=${LDPATCHER_WORKSPACE_ROOT:-/c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32}
SRC=${LDPATCHER_SOURCE_ROOT:-}
BLD=${LDPATCHER_BUILD_DIR:-$ROOT/manual_build_canonical/_build-ld-st-canonical}
INS=${LDPATCHER_INSTALL_DIR:-$ROOT/manual_build_canonical/_install-ld-st-canonical}
LP=$BLD/liblongpath-win32
JOBS=${JOBS:-$(nproc)}

export PATH=/mingw64/bin:/usr/bin

if [ -z "$SRC" ]; then
  echo "LDPATCHER_SOURCE_ROOT is required. Point it at an extracted ST source tree." >&2
  exit 2
fi

rm -rf "$BLD" "$INS"
mkdir -p "$BLD" "$INS"

cd "$SRC/src/liblongpath-win32"
python ./helper.py --generate "$LP" --triplet x86_64-w64-mingw32

cd "$BLD"
export CFLAGS="-I$SRC/src/liblongpath-win32/include"
export CPPFLAGS="-I$SRC/src/liblongpath-win32/include"
export LDFLAGS_WRAP_FILEIO="@$LP/gcc/exe.inputs"
export LDFLAGS_DLLWRAP_FILEIO="@$LP/gcc/dll.inputs"

"$SRC/src/binutils/configure" \
  --prefix="$INS" \
  --build=x86_64-w64-mingw32 \
  --host=x86_64-w64-mingw32 \
  --target=arm-none-eabi \
  --disable-gdb --disable-sim \
  --disable-nls \
  --enable-plugins \
  --with-sysroot="$INS/arm-none-eabi" \
  --with-pkgversion="GNU Tools for STM32 canonical-longpath" \
  --disable-werror

make -j"$JOBS" MAKEINFO=true all-ld
make MAKEINFO=true install-ld
