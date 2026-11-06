#!/usr/bin/env bash
# Builds the WebAssembly demo into build-web/dist (run inside an Emscripten environment).
#
#   docker run --rm -v "$PWD:/src" -w /src emscripten/emsdk:3.1.74 web/build.sh
#
# Two variants are produced: sr_web_mt (pthreads, needs cross-origin isolation)
# and sr_web_st (single thread fallback). index.html picks one at runtime.
set -euo pipefail
cd "$(dirname "$0")/.."

for threads in ON OFF; do
    emcmake cmake -S . -B "build-web/$threads" -DSR_WEB_THREADS=$threads -DCMAKE_BUILD_TYPE=Release
    cmake --build "build-web/$threads" --parallel
done

dist=build-web/dist
rm -rf "$dist"
mkdir -p "$dist"
cp web/index.html web/coi-serviceworker.js "$dist/"
cp build-web/ON/bin/sr_web_mt.* build-web/OFF/bin/sr_web_st.* "$dist/"
echo "Demo written to $dist"
