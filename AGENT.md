# AGENT.md (for EnergyTop)

## 1) 목적
- 이 저장소의 코딩 에이전트/개발자가 **빠르게 구현하고 안전하게 수정**하기 위한 최소 규칙.
- 대상: 개발용 Android 기기에서 shell로 직접 실행하는 `energytopd`/`energytop`.

## 2) 이번 버전 범위
- `ARCHITECTURE.md`의 전제를 따른다.
- 아래 이슈는 **Known Issue**로 유지한다: 일반 단말 권한/SELinux, 앱 생명주기, APK 모델, ABI 호환성, 정밀 캘리브레이션, suspend 최적화, 고급 백프레셔.

## 3) 초보자 친화 설계 원칙 (필수)
- 데몬 루프에는 오케스트레이션만 둔다. 계산/수집 로직을 직접 넣지 않는다.
- 기능별로 파일을 분리한다:
  - 통계 처리: `src/stats/power_stats.h|cpp`
  - 수집기 인터페이스: `src/collectors/collector.h`
  - 배터리 수집기: `src/collectors/battery_collector.h|cpp`
  - 확장 수집기(cpufreq/devfreq 등): `src/collectors/*_collector.h|cpp`
  - 수집기 등록: `src/collectors/collector_registry.h|cpp`

## 4) 통계 처리 규칙
- 전력 통계 계산은 `PowerStats` 클래스에만 구현한다.
- 최소 공개 함수:
  - `void add_sample(const Sample& s);`
  - `StatsSnapshot snapshot() const;`
  - `void reset();`
- 평균/누적/최소/최대 계산 코드는 데몬/CLI로 복사하지 않는다.

## 5) 통계 대상 추가 규칙 (cpufreq/devfreq 예시)
- 새 항목 추가는 아래 3단계만 수행:
  1. `ICollector` 구현 클래스 추가 (`CpuFreqCollector`, `DevFreqCollector`)
  2. `collector_registry`에 등록
  3. `PowerStats` 입력 구조(`Sample` 또는 `MetricSample`)에 필드 추가
- 기존 배터리 수집 코드를 수정해서 끼워 넣지 말고, **새 collector 파일**을 만든다.

## 6) 함수 설계 규칙
- 한 함수는 한 책임만 가진다.
- 추천 함수명:
  - `read_sysfs_int64(path)`
  - `collect_battery_sample()`
  - `collect_cpufreq_sample()`
  - `compute_power_uw(current_ua, voltage_uv)`
- 파일 I/O, 파싱, 계산, 직렬화를 한 함수에 섞지 않는다.

## 7) 테스트 최소선
- 통계 계산(`PowerStats`)은 단위 테스트를 우선 작성한다.
- 수집기 테스트는 sysfs mock 파일로 수행한다.

## 8) Cloud Agent 환경 준비 (필수)
- 이 저장소는 CMake + C++17 + ZeroMQ + inih 기반이다. 새 cloud agent 환경에서는 아래 패키지를 먼저 설치한다.
  - `libstdc++-14-dev`
  - `libzmq3-dev`
  - `cppzmq-dev`
  - `libinih-dev`
  - `libgtest-dev`
  - `ninja-build`
  - `pkg-config`
  - `cmake`
- 참고: 일부 Ubuntu 이미지에서 `/usr/bin/c++`가 `clang 18`을 가리키며 GCC 14 경로를 선택한다. 이때 `libstdc++-14-dev`가 없으면 링크 시 `cannot find -lstdc++`가 발생한다.

권장 초기 검증 순서:

```bash
printf 'int main(){return 0;}\n' > /tmp/cxx_link_test.cpp
c++ /tmp/cxx_link_test.cpp -o /tmp/cxx_link_test
/tmp/cxx_link_test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- 빌드 성공 시 installer 산출물(`build/energytop-installer-<arch>-<commit>.sh`)까지 생성되어야 한다.
