#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <binary-path> <android-abi> <ndk-root>" >&2
  exit 2
fi

BINARY_PATH="$1"
ANDROID_ABI="$2"
NDK_ROOT="$3"
READELF="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"

if [[ ! -x "${READELF}" ]]; then
  echo "error: llvm-readelf not found at ${READELF}" >&2
  exit 1
fi
if [[ ! -f "${BINARY_PATH}" ]]; then
  echo "error: binary not found: ${BINARY_PATH}" >&2
  exit 1
fi

case "${ANDROID_ABI}" in
  arm64-v8a)
    EXPECTED_MACHINE="AArch64"
    ;;
  x86_64)
    EXPECTED_MACHINE="Advanced Micro Devices X86-64"
    ;;
  *)
    echo "error: unsupported ABI '${ANDROID_ABI}'" >&2
    exit 1
    ;;
esac

if ! "${READELF}" -h "${BINARY_PATH}" | rg -q "Machine:\\s+${EXPECTED_MACHINE}"; then
  echo "error: ${BINARY_PATH} is not built for ${ANDROID_ABI}" >&2
  "${READELF}" -h "${BINARY_PATH}" >&2
  exit 1
fi

if ! "${READELF}" -l "${BINARY_PATH}" | rg -q "/system/bin/linker64"; then
  echo "error: ${BINARY_PATH} does not request Android linker64" >&2
  "${READELF}" -l "${BINARY_PATH}" >&2
  exit 1
fi

if ! "${READELF}" -h "${BINARY_PATH}" | rg -q "Type:\\s+DYN"; then
  echo "error: ${BINARY_PATH} is not PIE/DYN" >&2
  "${READELF}" -h "${BINARY_PATH}" >&2
  exit 1
fi

echo "Verified Android ELF: ${BINARY_PATH} (${ANDROID_ABI})"
