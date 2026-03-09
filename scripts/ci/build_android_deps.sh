#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <android-abi> <android-api> <install-prefix>" >&2
  exit 2
fi

ABI="$1"
ANDROID_API="$2"
INSTALL_PREFIX="$3"

ZMQ_VERSION="4.3.5"
CPPZMQ_VERSION="4.10.0"
INIH_VERSION="r58"

NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"
if [[ -z "${NDK_ROOT}" ]]; then
  echo "error: ANDROID_NDK_ROOT (or ANDROID_NDK_HOME) is not set" >&2
  exit 1
fi

TOOLCHAIN_BIN="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin"
if [[ ! -d "${TOOLCHAIN_BIN}" ]]; then
  echo "error: NDK toolchain not found at ${TOOLCHAIN_BIN}" >&2
  exit 1
fi

case "${ABI}" in
  arm64-v8a)
    CLANG_TRIPLE="aarch64-linux-android"
    ;;
  x86_64)
    CLANG_TRIPLE="x86_64-linux-android"
    ;;
  *)
    echo "error: unsupported Android ABI '${ABI}'" >&2
    exit 1
    ;;
esac

CC="${TOOLCHAIN_BIN}/${CLANG_TRIPLE}${ANDROID_API}-clang"
AR="${TOOLCHAIN_BIN}/llvm-ar"
READELF="${TOOLCHAIN_BIN}/llvm-readelf"

if [[ ! -x "${CC}" || ! -x "${AR}" || ! -x "${READELF}" ]]; then
  echo "error: required NDK tools are missing" >&2
  exit 1
fi

mkdir -p "${INSTALL_PREFIX}"
VERSION_STAMP="${INSTALL_PREFIX}/.versions"
EXPECTED_STAMP="libzmq=${ZMQ_VERSION}
cppzmq=${CPPZMQ_VERSION}
inih=${INIH_VERSION}
abi=${ABI}
api=${ANDROID_API}"

if [[ -f "${VERSION_STAMP}" ]] && [[ "$(cat "${VERSION_STAMP}")" == "${EXPECTED_STAMP}" ]]; then
  echo "Android deps are up-to-date at ${INSTALL_PREFIX}"
  exit 0
fi

rm -rf "${INSTALL_PREFIX}"
mkdir -p "${INSTALL_PREFIX}/include" "${INSTALL_PREFIX}/lib"

WORK_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

echo "Building libzmq ${ZMQ_VERSION} for ${ABI} (android-${ANDROID_API})"
curl -fsSL "https://github.com/zeromq/libzmq/archive/refs/tags/v${ZMQ_VERSION}.tar.gz" \
  -o "${WORK_DIR}/libzmq.tar.gz"
tar -xzf "${WORK_DIR}/libzmq.tar.gz" -C "${WORK_DIR}"

cmake -S "${WORK_DIR}/libzmq-${ZMQ_VERSION}" -B "${WORK_DIR}/libzmq-build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${NDK_ROOT}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="${ABI}" \
  -DANDROID_PLATFORM="android-${ANDROID_API}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
  -DBUILD_SHARED=OFF \
  -DBUILD_STATIC=ON \
  -DZMQ_BUILD_TESTS=OFF \
  -DWITH_LIBSODIUM=OFF \
  -DENABLE_CURVE=OFF

cmake --build "${WORK_DIR}/libzmq-build" --parallel
cmake --install "${WORK_DIR}/libzmq-build"

echo "Installing cppzmq ${CPPZMQ_VERSION} header"
curl -fsSL "https://raw.githubusercontent.com/zeromq/cppzmq/v${CPPZMQ_VERSION}/zmq.hpp" \
  -o "${INSTALL_PREFIX}/include/zmq.hpp"

echo "Building inih ${INIH_VERSION} for ${ABI}"
curl -fsSL "https://raw.githubusercontent.com/benhoyt/inih/${INIH_VERSION}/ini.c" \
  -o "${WORK_DIR}/ini.c"
curl -fsSL "https://raw.githubusercontent.com/benhoyt/inih/${INIH_VERSION}/ini.h" \
  -o "${INSTALL_PREFIX}/include/ini.h"
"${CC}" -O2 -fPIC -I"${INSTALL_PREFIX}/include" -c "${WORK_DIR}/ini.c" -o "${WORK_DIR}/ini.o"
"${AR}" rcs "${INSTALL_PREFIX}/lib/libinih.a" "${WORK_DIR}/ini.o"

echo "${EXPECTED_STAMP}" > "${VERSION_STAMP}"
echo "Android deps installed to ${INSTALL_PREFIX}"
