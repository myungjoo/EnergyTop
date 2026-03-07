# Architecture & Design: EnergyTop

## 1. System Overview
**EnergyTop**은 Linux Kernel sysfs 기반의 전력 데이터를 소프트웨어적으로 폴링하여 기록하고 표출하는 Android On-device 전력 모니터링 시스템이다. 고전류(10A 이상) 모바일 디바이스(예: 갤럭시 S25, S26 등)에서 외부 하드웨어 장비 없이 독립적으로 동작하며, 앱 동작 및 시스템 이벤트와의 교차 분석을 위해 타임스탬프 기반의 정밀한 로깅을 지원한다.

### 1.0. Version Scope & Operating Assumptions (이번 버전 전제)
본 문서는 **개발용 기기**에서 **시스템 SW 개발자가 shell로 직접 설치/실행**하는 사용 시나리오를 전제로 한다.

* 실행 형태: Android APK 앱이 아닌, shell에서 직접 `energytopd`/`energytop`를 실행하는 CLI 기반 도구
* 권한 전제: 대상 sysfs 및 데이터 경로 접근에 필요한 권한이 이미 확보되어 있다고 가정
* 프로세스 생존성: 개발자가 shell에서 daemon을 직접 관리하므로 Android 앱 생명주기 제약은 본 버전 범위에서 제외
* 호환성 전제: 본 버전은 단일 개발 환경/동일 빌드 체인을 기준으로 사용하며, 장기 ABI/프로토콜 호환성은 우선순위에서 제외



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

## 6. Known Issues (Intentionally Deferred in This Version)
아래 항목은 본 버전에서 **Known Issue로 인지하고 의도적으로 미해결 상태로 유지**한다.

1. **일반 사용자 단말 권한/SELinux 제약 대응 미포함**
   * 사유: 개발용 기기, shell 직접 실행 전제
2. **Android 앱 생명주기/백그라운드 제약 대응 미포함**
   * 사유: daemon을 shell에서 직접 구동/관리
3. **APK 배포 모델 및 앱 샌드박스 경로 구조 미적용**
   * 사유: 본 SW는 앱이 아닌 시스템 개발자용 CLI 도구
4. **ZMQ 전송 포맷의 ABI/버전 호환성 강화(예: 스키마 직렬화) 미적용**
   * 사유: 현 버전은 동일 환경 내 사용을 전제
5. **기기별 측정 정확도 편차/캘리브레이션 체계 고도화 미적용**
   * 사유: 현 단계 우선순위 제외
6. **Suspend/Sleep 상태 전력 측정 최적화 미적용**
   * 사유: 해당 상태 전력 소비는 현 버전 관심 범위 외
7. **버퍼 백프레셔/유실 정책 고도화 미적용**
   * 사유: 현 단계 우선순위 제외

> 위 항목들은 차기 버전에서 운영 환경/배포 모델 요구사항이 확정되면 우선순위에 따라 순차 반영한다.
