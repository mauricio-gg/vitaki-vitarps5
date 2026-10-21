#!/usr/bin/env bash
# Builds every host test in test/ natively and runs them all with ctest.
# Usage: ./tools/native_tests.sh   (needs cmake, a C compiler and OpenSSL dev files)
# The build goes to build-native-tests/ (git-ignored). It is a Debug build so assert() is live.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-native-tests"

# Homebrew OpenSSL is keg-only on macOS; CMake only finds it when told where it is.
if [ -z "${OPENSSL_ROOT_DIR:-}" ] && command -v brew >/dev/null 2>&1; then
    OPENSSL_ROOT_DIR="$(brew --prefix openssl@3 2>/dev/null || true)"
fi

git -C "$ROOT" submodule update --init vita/third_party/tomlc99

cmake -S "$ROOT/test" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
    ${OPENSSL_ROOT_DIR:+-DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR"}
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
