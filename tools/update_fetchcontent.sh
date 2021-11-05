#!/bin/bash

declare -a levk_libs=(ktl dtest)
declare -a lib_dirs=(libs/decf libs/dtasks)

options=$1

cd "$(dirname "${BASH_SOURCE[0]}")"
. ./os.sh

exe=update-fetchcontent
[ "$is_win64" == "true" ] && exe=$exe.exe

if [ ! -f src/update_fetchcontent/out/rl/CMakeCache.txt ]; then
  echo "== Running cmake to configure release preset"
  cmake src/update_fetchcontent --preset=release -Dclap_DIR=../../../out/rl/libs/clap
  if [[ $? != 0 ]]; then
    echo -e "\n== Configuration failed! Removing CMake cache"
    rm src/update_fetchcontent/out/rl/CMakeCache.txt
    exit 10
  fi
fi

cmake --build src/update_fetchcontent/out/rl || exit 10
cmake --install src/update_fetchcontent/out/rl --prefix=bin || exit 10

if [ ! -f bin/$exe ]; then
  echo -e "\n== Required tool not found: bin/$exe"
  exit 10
fi

cd ..

for lib in "${levk_libs[@]}"; do
  tools/bin/$exe $options $lib libs/$lib
done

for lib_dir in "${lib_dirs[@]}"; do
  tools/bin/$exe $options ktl libs/ktl -c=$lib_dir/CMakeLists.txt
done