#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$root/build"

sources=(src/main.cpp src/cl_backend.cpp src/vk_interop.cpp src/scene.cpp src/bvh.cpp src/renderer.cpp src/image.cpp)

if command -v clang++ >/dev/null 2>&1; then cxx=clang++
elif command -v g++ >/dev/null 2>&1; then cxx=g++
else echo "no C++ compiler found (need clang++ or g++)" >&2; exit 1
fi

flags=(-std=c++20 -O2 -Wall -Wextra -Isrc)
libs=()
case "$(uname -s)" in
  Darwin) flags+=(-DCL_SILENCE_DEPRECATION) ;;
  Linux)  libs+=(-ldl) ;;
esac

cd "$root"
"$cxx" "${flags[@]}" "${sources[@]}" -o build/ember "${libs[@]}"
echo "built: $root/build/ember"
