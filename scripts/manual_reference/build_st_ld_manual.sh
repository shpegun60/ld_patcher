#!/usr/bin/env bash
set -euo pipefail

ROOT=${LDPATCHER_WORKSPACE_ROOT:-/c/Users/admin/Documents/my_workspace/gnu/gnu-tools-for-stm32}
SRC=${LDPATCHER_SOURCE_ROOT:-}
OUT=${LDPATCHER_OUT_DIR:-$ROOT/manual_build}
BLD=${LDPATCHER_BUILD_DIR:-$OUT/_build-ld-st-manual}
INS=${LDPATCHER_INSTALL_DIR:-$OUT/_install-ld-st-manual}
DROP=${LDPATCHER_DROP_DIR:-$OUT/_cubeide-arm-linker-st-manual-jsonpatch}
PKGVER=${LDPATCHER_DISPLAY_VERSION:-manual}
JOBS=${JOBS:-$(nproc)}

export PATH=/mingw64/bin:/usr/bin

if [ -z "$SRC" ]; then
  echo "LDPATCHER_SOURCE_ROOT is required. Point it at an extracted ST source tree." >&2
  exit 2
fi

echo "LDPATCHER_PROGRESS 5 Preparing build directories"
rm -rf "$BLD" "$INS" "$DROP"
mkdir -p "$BLD" "$INS" "$DROP"

cd "$BLD"
export CFLAGS="-I$SRC/src/liblongpath-win32/include"
export CPPFLAGS="-I$SRC/src/liblongpath-win32/include"

echo "LDPATCHER_PROGRESS 15 Configuring binutils tree"
"$SRC/src/binutils/configure" \
  --prefix="$INS" \
  --build=x86_64-w64-mingw32 \
  --host=x86_64-w64-mingw32 \
  --target=arm-none-eabi \
  --disable-gdb \
  --disable-sim \
  --disable-nls \
  --enable-plugins \
  --with-sysroot="$INS/arm-none-eabi" \
  --with-pkgversion="GNU Tools for STM32 $PKGVER" \
  --disable-werror

echo "LDPATCHER_PROGRESS 45 Building ld"
make -j"$JOBS" MAKEINFO=true all-ld
echo "LDPATCHER_PROGRESS 75 Installing ld"
make MAKEINFO=true install-ld

echo "LDPATCHER_PROGRESS 90 Collecting runtime artifacts"
cp "$INS/bin/arm-none-eabi-ld.exe" "$DROP/ld.exe"
cp "$INS/bin/arm-none-eabi-ld.bfd.exe" "$DROP/ld.bfd.exe"
cp "$INS/bin/arm-none-eabi-ld.exe" "$DROP/arm-none-eabi-ld.exe"
cp "$INS/bin/arm-none-eabi-ld.bfd.exe" "$DROP/arm-none-eabi-ld.bfd.exe"
cp /mingw64/bin/libwinpthread-1.dll "$DROP/libwinpthread-1.dll"
cp /mingw64/bin/libzstd.dll "$DROP/libzstd.dll"

echo "LDPATCHER_PROGRESS 98 Checking ld.exe --help"
"$DROP/ld.exe" --help | grep -q "dump-script-json"
echo "LDPATCHER_PROGRESS 100 Manual build completed"
