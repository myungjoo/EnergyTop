# EnergyTop

EnergyTop은 Linux/Android 환경에서 배터리 전류(`current_now`)와 전압(`voltage_now`)을 주기적으로 수집해,  
실시간 모니터링 화면과 CSV 로그로 제공하는 C++ 기반 전력 모니터링 도구입니다.

## 1. 프로그램 목적

- 배터리 전류/전압 데이터를 **저오버헤드로 지속 수집**
- 수집 데이터를 ZeroMQ로 발행하여 `energytop`에서 **실시간 확인**
- 데이터를 CSV로 저장하고 크기 초과 시 회전/압축하여 **장시간 기록**
- 앱 실행, 시스템 이벤트 등과 전력 변화를 교차 분석할 수 있는 기반 제공

구성 요소:

- `energytopd` (데몬): sysfs 폴링 → ZeroMQ 발행 → CSV 기록/로테이션
- `energytop` (CLI): ZeroMQ 구독 → 실시간 통계 표시(샘플 수, 평균/최소/최대 전력)

---

## 2. 사용방법

### 2.1 빌드

필수 의존성(예시):

- CMake (3.20+)
- C++17 컴파일러
- libzmq, cppzmq 헤더(`zmq.hpp`)
- inih

빌드:

```bash
cmake -S . -B build
cmake --build build -j
```

빌드 후 실행 파일:

- `build/energytopd`
- `build/energytop`
- `build/energytop-installer-<arch>-<commit>.sh` (설치용 self-extracting 스크립트)

설치 스크립트 실행(타깃 디바이스):

```bash
sudo ./energytop-installer-x64-<commit>.sh
```

설치 위치:

- `/usr/bin/energytop`
- `/usr/bin/energytopd`
- `/etc/energytop.ini`

테스트/스테이징 설치:

```bash
./energytop-installer-x64-<commit>.sh --prefix /tmp/energytop-stage
```

### 2.2 설정 파일

기본 설정 파일 예시는 저장소의 `energytop.ini`를 사용하면 됩니다.

설정 파일 탐색 우선순위:

1. CLI 인자 `--config PATH`
2. `/etc/energytop.ini`
3. `/data/local/tmp/energytop.ini`

주요 설정 키:

- `[Daemon] daemon_polling_interval_ms`
- `[Daemon] zmq_publish_interval_sec`
- `[Daemon] zmq_endpoint`
- `[Hardware] sysfs_path_override`
- `[Hardware] invert_current_sign`
- `[Storage] csv_output_path`
- `[Storage] csv_max_size_mb`

### 2.3 실행

데몬:

```bash
./build/energytopd [--config PATH] [--duration-sec N]
```

- `--duration-sec N`: N초 후 자동 종료(미지정 시 계속 실행)

모니터:

```bash
./build/energytop [--config PATH] [--once]
```

- `--once`: 메시지 1회 수신 후 출력하고 종료
- 기본 실행 시 키 입력으로 5개 독립 구간(Window 1~5)을 제어
  - `1`~`5`: 해당 윈도우 start/stop 토글
  - stop 상태에서 같은 키 재입력 시 reset 후 새 구간 시작
  - `q`: 모니터 종료

---

## 3. 사용 예시

### 예시 1) 기본 설정으로 데몬 실행

```bash
./build/energytopd
```

### 예시 2) 커스텀 설정으로 15초만 실행

```bash
./build/energytopd --config ./energytop.ini --duration-sec 15
```

### 예시 3) 모니터를 1회 샘플만 출력하고 종료

```bash
./build/energytop --config ./energytop.ini --once
```

### 예시 4) 모니터를 상시 실행(실시간 화면 갱신)

```bash
./build/energytop --config ./energytop.ini
```

---

## 4. 출력 예시

### 4.1 `energytop` 터미널 출력 예시

아래와 같은 형식으로 실시간 화면이 갱신됩니다.

```text
EnergyTop Monitor
samples      : 150
latest current(uA): -482000
latest voltage(uV): 3875000
avg power(uW): -1821500
min/max(uW)  : -2411200 / -905000
energy(uJ)   : -271200
refresh(ms)  : 200
keys         : [1-5] window start/stop(toggle), [q] quit

Window intervals
ID  STATE  samples         avg(uW)          energy(uJ)      elapsed
 1  IDLE         0               0                 0  -
 2  RUN        124        -1765000           -983221  2.473s
 3  STOP       251        -1698200          -3519821  5.007s
 4  IDLE         0               0                 0  -
 5  IDLE         0               0                 0  -
```

### 4.2 CSV 로그 출력 예시 (`csv_output_path`)

```csv
timestamp_boot_ns,timestamp_real_ms,current_ua,voltage_uv
5242912039481,1741391910123,-481000,3876000
5242912139562,1741391910223,-483000,3875000
5242912239655,1741391910323,-479000,3875000
```

### 4.3 잘못된 인자 전달 시 사용법 출력 예시

```text
Usage: energytopd [--config PATH] [--duration-sec N]
Usage: energytop [--config PATH] [--once]
```
