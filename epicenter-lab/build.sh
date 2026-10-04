#!/usr/bin/env bash
# Builds epicenter_lab.exe (statically linked so it's self-contained / portable).
set -e
cd "$(dirname "$0")"

GPP="$(command -v g++ 2>/dev/null || true)"
if [ -z "$GPP" ]; then
  GPP="$(ls /c/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT*/mingw64/bin/g++.exe 2>/dev/null | head -1)"
fi
if [ -z "$GPP" ]; then
  echo "g++ not found. Install with: winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT"
  exit 1
fi

"$GPP" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ \
  main.cpp EpicenterDSPCore.cpp -o epicenter_lab.exe
echo "built epicenter_lab.exe  (compiler: $GPP)"
