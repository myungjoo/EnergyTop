# Architecture & Design: EnergyTop

## 1. System Overview
**EnergyTop**은 Linux Kernel sysfs 기반의 전력 데이터를 소프트웨어적으로 폴링하여 기록하고 표출하는 Android On-device 전력 모니터링 시스템이다. 고전류(10A 이상) 모바일 디바이스(예: 갤럭시 S25, S26 등)에서 외부 하드웨어 장비 없이 독립적으로 동작하며, 앱 동작 및 시스템 이벤트와의 교차 분석을 위해 타임스탬프 기반의 정밀한 로깅을 지원한다.



### 1.1. Core Components
* **`energytopd` (Daemon):** 백그라운드에서 동작하며 sysfs에서 주기적으로 데이터를 읽고, CSV 파일로 저장 및 순환(Rotation) 관리하며, 설정된 주기(기본 5초)마다 데이터를 모아 ZeroMQ를 통해 브로드캐스트하는 메인 프로세스.
* **`energytop` (CLI Monitor):** 터미널 환경에서 실행되는 클라이언트로, ZeroMQ `SUB` 소켓을 통해 데몬이 퍼블리시한 배치(Batch) 데이터를 수신받아 `top` 명령어와 같이 실시간 전력 상황을 터미널에 표출하는 실행 파일.

## 2. Component Design & Responsibilities

### 2.1. `energytopd` (Daemon)
* **Data Source & I/O Optimization:**
    * 타겟 노드: `/sys/class/power_supply/battery/` (기본값)
    * 타겟 파일: `current_now` ($\mu A$), `voltage_now` ($\mu V$)
    * 최적화: 데몬 초기화 시 대상 파일의 File Descriptor(FD)를 열어두고, 매 폴링 시 `lseek(fd, 0, SEEK_SET)`와 `read()`를 사용하여 커널 스위칭 오버헤드를 최소화한다.
* **Data Batching & IPC (ZeroMQ):**
    * 잦은 IPC 호출을 방지하기 위해, 메모리 내 버퍼(`std::vector<PowerRecord>`)에 폴링 데이터를 순차적으로 적재한다.
    * `zmq_publish_interval_sec` 주기마다 버퍼에 쌓인 데이터를 하나의 ZMQ 메시지로 패킹하여 `PUB` 소켓으로 전송하고 버퍼를 초기화한다.
* **Data Management (CSV Logging & Rotation):**
    * 수집된 레코드를 지정된 CSV 파일에 Append 한다.
    * **Rotation & Compression:** 파일 크기가 `csv_max_size_mb` (기본 50MB)를 초과하면, 현재 파일을 닫고 새로운 파일을 생성한다. 기존 파일은 백그라운드 스레드에서 분할 압축(gzip 등)하여 스토리지 고갈을 방지한다.

### 2.2. `energytop` (CLI Monitor)
* **ZeroMQ Subscription & Display:**
    * `SUB` 소켓으로 데몬의 배치 데이터를 수신받아 파싱한다.
    * ncurses 또는 ANSI Escape 코드를 사용하여 화면을 갱신하며, 배치 데이터 내의 최신 값 및 해당 기간 내의 통계(평균 전력 등)를 계산하여 표출한다.

## 3. Configuration Management

Android 시스템의 `/etc` (실제 `/system/etc`)는 Read-Only 파티션이므로, 설정 파일은 권한에 따라 유연하게 탐색 및 폴백(Fallback) 처리되어야 한다.

* **Configuration Path Resolution (우선순위 순):**
    1. CLI Argument로 전달된 경로 (예: `--config /path/to/config.ini`)
    2. `/etc/energytop.ini` (루팅 환경 및 벤더 이미지 빌드 시)
    3. `/data/local/tmp/energytop.ini` (일반적인 adb shell 개발 환경 폴백)

### 3.1. Default `energytop.ini` Example
```ini
[Daemon]
# sysfs에서 값을 읽어오는 주기 (단위: ms)
daemon_polling_interval_ms = 100

# 읽어온 데이터를 모아서 ZeroMQ로 브로드캐스트하는 주기 (단위: sec)
zmq_publish_interval_sec = 5

# ZeroMQ Pub/Sub 통신 주소 (향후 tcp://*:5555 등으로 확장 가능)
zmq_endpoint = ipc:///data/local/tmp/energytop.ipc

[Hardware]
# 배터리 노드 경로 (값이 비어있으면 기본 경로 사용)
# 기본 경로: /sys/class/power_supply/battery/
sysfs_path_override = 

# [중요] PMIC 제조사 및 드라이버 구현에 따른 전류 부호 파편화 대응
# true로 설정 시 current_now 값의 부호를 반전(x -1)시켜 계산 및 저장함.
# 방전 중일 때 current_now가 양수(+)로 나오는 기기라면 true로 설정할 것.
invert_current_sign = false

[Storage]
# 데이터가 저장될 CSV 파일의 기본 경로 및 접두사
csv_output_path = /data/local/tmp/energytop_log.csv

# CSV 파일의 최대 크기 (단위: MB). 
# 이 크기를 초과하면 파일을 분할하고 과거 파일은 백그라운드에서 압축(.gz) 처리됨.
csv_max_size_mb = 50
```

## 4. Data Structures & Serialization
코딩 에이전트는 ZeroMQ로 데이터를 전송할 때 아래 구조체의 배열(Array of Structs) 형태로 패킹하여 전송해야 한다.

```C++
#include <stdint.h>

// 단일 측정 레코드
// 총 24 bytes 크기 구조체 (Padding 주의, #pragma pack 고려)
struct PowerRecord {
    uint64_t timestamp_boot_ns; // CLOCK_BOOTTIME (시스템 이벤트 동기화 용도)
    uint64_t timestamp_real_ms; // CLOCK_REALTIME (Wall-clock, 사람이 읽기 위한 용도)
    int32_t current_ua;         // current_now (Microampere, 부호 보정 완료된 값)
    int32_t voltage_uv;         // voltage_now (Microvolt)
};
```

## 5. Implementation Guidelines for Coding Agent
### 5.1. Language & Tools: C++17 이상, CMake 빌드 시스템 사용. Android NDK 환경에서의 크로스 컴파일을 고려하여 작성할 것.

### 5.2. Dependencies: libzmq & cppzmq, inih (INI 파싱).

### 5.3. Threading Model (energytopd):

Thread 1 (Poller): 타이머 기반으로 sysfs를 읽고 버퍼(std::vector<PowerRecord>)에 누적. invert_current_sign 설정에 따른 전처리 수행.

Thread 2 (Publisher & Logger): ZMQ 배치 주기에 맞춰 Thread 1의 버퍼와 빈 버퍼를 std::swap (Lock 소유 시간 최소화). 가져온 데이터를 ZMQ로 송신 및 CSV에 Write.

Thread 3 (Compressor - Optional but Recommended): 파일 크기가 csv_max_size_mb를 넘을 때, 메인 로깅 스레드 블로킹을 막기 위해 파일명 변경(Rename) 및 압축(zlib 또는 system("gzip ..."))을 비동기로 수행.

### 5.4. Error Handling: 타겟 sysfs 파일 누락, 권한 부족, ZMQ 바인딩 실패 시 명확한 에러 메시지를 stderr 및 logcat에 남기고 Graceful shutdown 처리할 것.
