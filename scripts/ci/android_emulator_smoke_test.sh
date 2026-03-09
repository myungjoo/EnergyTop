#!/usr/bin/env bash
set -euo pipefail

timestamp() {
  date -u +"%Y-%m-%dT%H:%M:%SZ"
}

log_step() {
  echo "[android-smoke][$(timestamp)] $*"
}

trap 'log_step "FAILED at line ${LINENO} (exit=$?)"' ERR

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <android-dist-dir>" >&2
  exit 2
fi

log_step "Step 1/9: validate input artifacts"
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
REMOTE_TCP_PORT="35555"
REMOTE_INSTALL_PREFIX="/data/local/tmp/energytop-installed"

log_step "Step 2/9: wait for emulator and prepare remote directories"
adb wait-for-device
adb shell "rm -rf '${REMOTE_BASE}' '${REMOTE_INSTALL_PREFIX}' && mkdir -p '${REMOTE_BASE}' '${REMOTE_SYSFS}'"

log_step "Step 3/9: create mock sysfs and test config"
adb shell "printf '1000000\n' > '${REMOTE_SYSFS}/current_now'"
adb shell "printf '4000000\n' > '${REMOTE_SYSFS}/voltage_now'"

cat > /tmp/energytop-android-test.ini <<EOF
[Daemon]
daemon_polling_interval_ms = 50
zmq_publish_interval_sec = 1
zmq_endpoint = tcp://127.0.0.1:${REMOTE_TCP_PORT}

[Hardware]
sysfs_path_override = ${REMOTE_SYSFS}
invert_current_sign = false

[Storage]
csv_output_path = ${REMOTE_LOG}
csv_max_size_mb = 4
EOF

log_step "Step 4/9: push binaries, installer, and config to emulator"
adb push "${DIST_DIR}/energytop-android-x64" "${REMOTE_BASE}/energytop" >/dev/null
adb push "${DIST_DIR}/energytopd-android-x64" "${REMOTE_BASE}/energytopd" >/dev/null
adb push "${DIST_DIR}/energytop-installer-android-x64.sh" "${REMOTE_BASE}/energytop-installer-android-x64.sh" >/dev/null
adb push "${DIST_DIR}/energytop.ini" "${REMOTE_BASE}/energytop.ini" >/dev/null
adb push /tmp/energytop-android-test.ini "${REMOTE_CONFIG}" >/dev/null
adb shell "chmod 0755 '${REMOTE_BASE}/energytop' '${REMOTE_BASE}/energytopd' '${REMOTE_BASE}/energytop-installer-android-x64.sh'"

log_step "Step 5/9: run direct binaries smoke test"
adb shell "cd '${REMOTE_BASE}' && ./energytopd --config '${REMOTE_CONFIG}' --duration-sec 4 > daemon.log 2>&1 &"
sleep 2
log_step "Step 5/9: waiting for first monitor output (timeout 60s)"
if ! timeout 60s adb shell "cd '${REMOTE_BASE}' && ./energytop --config '${REMOTE_CONFIG}' --once > monitor.out 2>&1"; then
  echo "error: timed out waiting for first monitor output" >&2
  adb shell "cd '${REMOTE_BASE}' && echo '--- daemon.log ---' && sed -n '1,200p' daemon.log && echo '--- monitor.out ---' && sed -n '1,200p' monitor.out" || true
  exit 1
fi
sleep 3

log_step "Step 6/9: validate direct-run outputs"
adb shell "test -s '${REMOTE_LOG}'"
adb shell "awk 'NR==1 && \$0!=\"timestamp_boot_ns,timestamp_real_ms,current_ua,voltage_uv\" { exit 1 } END { if (NR < 2) exit 2 }' '${REMOTE_LOG}'"
adb shell "cd '${REMOTE_BASE}' && grep -q 'EnergyTop Monitor' monitor.out"

log_step "Step 7/9: run Android installer and validate installed files"
adb shell "cd '${REMOTE_BASE}' && ./energytop-installer-android-x64.sh --prefix '${REMOTE_INSTALL_PREFIX}' > installer.log 2>&1"
adb shell "test -x '${REMOTE_INSTALL_PREFIX}/bin/energytop'"
adb shell "test -x '${REMOTE_INSTALL_PREFIX}/bin/energytopd'"
adb shell "test -f '${REMOTE_INSTALL_PREFIX}/etc/energytop.ini'"

log_step "Step 8/9: run installed binaries smoke test"
adb shell "'${REMOTE_INSTALL_PREFIX}/bin/energytopd' --config '${REMOTE_CONFIG}' --duration-sec 3 > '${REMOTE_BASE}/daemon-installed.log' 2>&1 &"
sleep 2
log_step "Step 8/9: waiting for installed monitor output (timeout 60s)"
if ! timeout 60s adb shell "'${REMOTE_INSTALL_PREFIX}/bin/energytop' --config '${REMOTE_CONFIG}' --once > '${REMOTE_BASE}/monitor-installed.out' 2>&1"; then
  echo "error: timed out waiting for installed monitor output" >&2
  adb shell "cd '${REMOTE_BASE}' && echo '--- daemon-installed.log ---' && sed -n '1,200p' daemon-installed.log && echo '--- monitor-installed.out ---' && sed -n '1,200p' monitor-installed.out" || true
  exit 1
fi

log_step "Step 9/9: final output validation"
adb shell "cd '${REMOTE_BASE}' && grep -q 'EnergyTop Monitor' monitor-installed.out"
adb shell "test -s '${REMOTE_LOG}'"

log_step "Android x64 emulator smoke tests passed"
