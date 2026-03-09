#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <android-dist-dir>" >&2
  exit 2
fi

DIST_DIR="$1"
if [[ ! -d "${DIST_DIR}" ]]; then
  echo "error: dist dir not found: ${DIST_DIR}" >&2
  exit 1
fi

for required in energytop-android-x64 energytopd-android-x64 energytop.ini energytop-installer-android-x64.sh; do
  if [[ ! -f "${DIST_DIR}/${required}" ]]; then
    echo "error: missing required file ${DIST_DIR}/${required}" >&2
    exit 1
  fi
done

REMOTE_BASE="/data/local/tmp/energytop-ci"
REMOTE_SYSFS="${REMOTE_BASE}/mock_sysfs"
REMOTE_CONFIG="${REMOTE_BASE}/energytop-android.ini"
REMOTE_LOG="${REMOTE_BASE}/energytop_log.csv"
REMOTE_IPC="${REMOTE_BASE}/energytop.ipc"
REMOTE_INSTALL_PREFIX="/data/local/tmp/energytop-installed"

adb wait-for-device
adb shell "rm -rf '${REMOTE_BASE}' '${REMOTE_INSTALL_PREFIX}' && mkdir -p '${REMOTE_BASE}' '${REMOTE_SYSFS}'"

adb shell "printf '1000000\n' > '${REMOTE_SYSFS}/current_now'"
adb shell "printf '4000000\n' > '${REMOTE_SYSFS}/voltage_now'"

cat > /tmp/energytop-android-test.ini <<EOF
[Daemon]
daemon_polling_interval_ms = 50
zmq_publish_interval_sec = 1
zmq_endpoint = ipc://${REMOTE_IPC}

[Hardware]
sysfs_path_override = ${REMOTE_SYSFS}
invert_current_sign = false

[Storage]
csv_output_path = ${REMOTE_LOG}
csv_max_size_mb = 4
EOF

adb push "${DIST_DIR}/energytop-android-x64" "${REMOTE_BASE}/energytop" >/dev/null
adb push "${DIST_DIR}/energytopd-android-x64" "${REMOTE_BASE}/energytopd" >/dev/null
adb push "${DIST_DIR}/energytop-installer-android-x64.sh" "${REMOTE_BASE}/energytop-installer-android-x64.sh" >/dev/null
adb push "${DIST_DIR}/energytop.ini" "${REMOTE_BASE}/energytop.ini" >/dev/null
adb push /tmp/energytop-android-test.ini "${REMOTE_CONFIG}" >/dev/null
adb shell "chmod 0755 '${REMOTE_BASE}/energytop' '${REMOTE_BASE}/energytopd' '${REMOTE_BASE}/energytop-installer-android-x64.sh'"

adb shell "cd '${REMOTE_BASE}' && ./energytopd --config '${REMOTE_CONFIG}' --duration-sec 4 > daemon.log 2>&1 &"
sleep 2
adb shell "cd '${REMOTE_BASE}' && ./energytop --config '${REMOTE_CONFIG}' --once > monitor.out 2>&1"
sleep 3

adb shell "test -s '${REMOTE_LOG}'"
adb shell "awk 'NR==1 && \$0!=\"timestamp_boot_ns,timestamp_real_ms,current_ua,voltage_uv\" { exit 1 } END { if (NR < 2) exit 2 }' '${REMOTE_LOG}'"
adb shell "cd '${REMOTE_BASE}' && grep -q 'EnergyTop Monitor' monitor.out"

adb shell "cd '${REMOTE_BASE}' && ./energytop-installer-android-x64.sh --prefix '${REMOTE_INSTALL_PREFIX}' > installer.log 2>&1"
adb shell "test -x '${REMOTE_INSTALL_PREFIX}/bin/energytop'"
adb shell "test -x '${REMOTE_INSTALL_PREFIX}/bin/energytopd'"
adb shell "test -f '${REMOTE_INSTALL_PREFIX}/etc/energytop.ini'"

adb shell "'${REMOTE_INSTALL_PREFIX}/bin/energytopd' --config '${REMOTE_CONFIG}' --duration-sec 3 > '${REMOTE_BASE}/daemon-installed.log' 2>&1 &"
sleep 2
adb shell "'${REMOTE_INSTALL_PREFIX}/bin/energytop' --config '${REMOTE_CONFIG}' --once > '${REMOTE_BASE}/monitor-installed.out' 2>&1"

adb shell "cd '${REMOTE_BASE}' && grep -q 'EnergyTop Monitor' monitor-installed.out"
adb shell "test -s '${REMOTE_LOG}'"

echo "Android x64 emulator smoke tests passed"
