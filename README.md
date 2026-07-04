# BLDC FOC Linux 커널 드라이버 프로젝트

> 이 문서는 프로젝트 전체 맥락, 배경, 목적, 구현 계획을 담은 참조 보고서입니다.
> 추후 Claude에게 이 문서를 첨부하면 대화 맥락 없이도 프로젝트를 이해하고 도움을 줄 수 있습니다.

---

## 1. 작성자 배경

### 현재 역량

| 영역 | 내용 | 수준 |
|---|---|---|
| 자동차 Application SW | 제어기 기능 로직 설계, AUTOSAR SWC/RTE/BSW 구조 이해 | 3년 실무 |
| 전력전자 제어 | TI DSP 레지스터 레벨 PWM/ADC 제어, Buck Converter 피드백 루프 구현 | 학부 연구인턴 |
| 통신 프로토콜 | SPI 통신 프로토콜 설계, 무선통신 IC 제어, Arduino 및 실무 테스트 프로그램 적용 | 실무 경험 |
| Linux / 드라이버 | 없음 | 미경험 |
| Android / AAOS | 없음 | 미경험 |

### 소프트웨어 계층 관점에서의 현재 위치

```
[ 자동차 Application SW (AUTOSAR SWC) ]   ← 3년 본업
[ OS / 미들웨어                         ]   ← 미경험
[ Linux 커널 / BSP / 드라이버           ]   ← 미경험 (이 프로젝트 목표)
[ 하드웨어 레지스터 직접 제어           ]   ← 학부 인턴 (DSP bare-metal)
                                              SPI 통신 IC 제어 경험
```

양 끝단 경험은 있으나 중간 계층(Linux 커널/드라이버)이 비어있는 구조.
이 프로젝트는 그 중간 계층을 채우는 것이 목적.

### AUTOSAR ↔ Linux 계층 대응 이해

작성자는 AUTOSAR 기반 개발자이므로 아래 대응 관계로 이해하면 빠름.

```
AUTOSAR                        Linux/Android
──────────────────────────────────────────────
[ SWC ]                    ↔  [ Android App ]
[ RTE ]                    ↔  [ Android Framework ]
[ ECU Abstraction Layer ]  ↔  [ HAL ]
[ MCAL ]                   ↔  [ Linux Kernel Driver ]  ← 이 프로젝트
[ 마이크로컨트롤러 ]        ↔  [ SoC 하드웨어 ]
```

학부 인턴 때 수행한 TI DSP 레지스터 직접 제어 = MCAL을 bare-metal로 직접 구현한 것과 동일한 작업. 이 프로젝트는 그 경험을 Linux 드라이버 계층으로 확장하는 것.

---

## 2. 프로젝트 목적

### 핵심 목표

> BLDC 모터 FOC 제어를 Linux 커널 드라이버로 직접 구현한다

### 선택 이유

**기술 스택 확장 측면**
- Linux Kernel Driver 경험 확보 (가장 큰 목적)
- 전력전자 제어 관심사와 하위 계층 SW 관심사를 하나의 프로젝트로 커버
- 학부 인턴 때 bare-metal로 구현한 PWM/ADC 제어를 Linux 드라이버로 재구현

---

## 3. 개발 환경

### 하드웨어 구성

| 부품 | 역할 | 선택 이유 | 예산 |
|---|---|---|---|
| BeagleBone Black | Linux 실행 타겟 | Raspberry Pi보다 PWM/ADC 핀에 Linux subsystem으로 직접 접근 유리 | 약 8만원 |
| BLDC 모터 | 제어 대상 | - | 약 2~5만원 |
| 3상 인버터 모듈 | 모터 구동 회로 | - | 약 3~5만원 |
| 전류 센서 (ACS712 등) | FOC 전류 피드백 | - | 약 1만원 |
| 엔코더 또는 홀센서 | 위치/속도 피드백 | 모터 내장 또는 별도 구매 | 약 1~2만원 |

총 예산: 약 15~20만원 내외

### 소프트웨어 개발 환경

| 항목 | 내용 |
|---|---|
| 호스트 OS | macOS (코드 작성) |
| 빌드 환경 | Docker 컨테이너 (Linux, 커널 모듈 크로스 컴파일 전용) |
| 타겟 OS | Linux (BeagleBone Black에 기본 설치된 Debian 12 이미지를 그대로 사용, 별도 이미지 빌드 없음) |
| 크로스 컴파일러 | arm-linux-gnueabihf-gcc (Docker 컨테이너 내부, apt로 설치) |
| 개발 도구 | VSCode (호스트), Docker Desktop, SSH (BBB 원격 접속), GDB + JTAG (디버깅) |
| 버전 관리 | Git / GitHub |

> **Yocto는 현재 사용하지 않음.** 초기 계획(v1)엔 Yocto로 커스텀 이미지를 빌드하는 안이 있었으나, 실제로 BBB에 접속해보니 이미 실행 중인 Debian 이미지 위에 커널 모듈(.ko)만 얹는 것으로 충분하다는 것을 확인함. Yocto는 "OS 이미지 자체를 새로 만들 때" 필요한 도구이지 "이미 있는 이미지에 드라이버 하나 추가할 때" 필요한 도구가 아니므로, 지금 단계에서는 제외.
>
> **왜 이 순서가 맞는가**: 드라이버/제어 로직처럼 빠른 피드백이 필요한 영역을 먼저 검증하고, 배포·패키징처럼 반복 주기가 긴 영역은 나중에 하는 게 표준적인 임베디드 개발 순서. Yocto부터 했다면 "드라이버 버그"와 "이미지 빌드 버그"가 뒤섞여 디버깅이 어려웠을 것. 또한 지금 당장 Yocto를 도입해도 배경 지식(커널/드라이버 기초)이 없는 상태라 제대로 이해하기 어려움 — 시스템을 먼저 동작시켜서 이해도를 쌓은 뒤 전환 여부를 판단하기로 결정함.
>
> **주의(솔직한 평가)**: 지금 계획대로 "이미 동작하는 드라이버를 레이어 하나로 감싸는" 정도로 Yocto를 도입하면, 실질적인 역량 확보는 얕을 수 있음. BBB는 이미 공식 BSP(`meta-ti`, `meta-beagleboard`)가 있어서 머신/디스트로 설정을 밑바닥부터 구성해볼 기회가 자동으로 주어지지 않고, 레시피 하나 추가하는 것만으로는 실무에서 부딪히는 문제(BitBake 의존성 트러블슈팅, 레이어 우선순위, 라이선스 매니페스트, 이미지 최적화)를 못 건드림. 나중에 전환을 고려할 때 이 점을 감안해서 스코프를 의도적으로 넓힐 것 ([6. 확장 로드맵](#6-확장-로드맵)의 "Yocto 기반 프로덕션 이미지화" 참조).

### 역할 분리 원칙

코드 작성(macOS) / 빌드(Docker, Linux) / 실행(BeagleBone)을 명확히 분리한다.

- **macOS**: 순수 텍스트 편집 환경. VSCode로 `.c`, `.dts`, `Makefile` 등을 작성. 커널 모듈을 macOS에서 직접 빌드하지 않는다.
- **Docker 컨테이너 (Linux)**: 실제 빌드가 일어나는 곳. 커널 모듈은 타겟과 동일한 커널 소스 트리/버전으로 빌드해야 insmod 시 버전 불일치 오류가 안 나므로, `bb-kernel`(RobertCNelson 빌드 프레임워크)로 타겟과 정확히 일치하는 커널 소스 트리를 이 컨테이너 안에서 준비하고, 그 트리를 기준으로 드라이버를 크로스 컴파일한다. macOS 프로젝트 디렉토리를 볼륨 마운트해서 컨테이너 안에서 그대로 빌드에 사용한다.
- **BeagleBone Black**: 순수 실행/테스트 타겟. 빌드된 `.ko` 파일을 전송받아 `insmod`, 동작 확인만 수행한다. BBB 자체 성능(Cortex-A8, 512MB RAM)으로는 커널 빌드가 비현실적이므로 빌드 작업을 절대 BBB에서 하지 않는다.

### Mac ↔ Docker ↔ BeagleBone 개발 워크플로우

```
[Mac - VSCode]
  코드 작성 (.c, .dts, Makefile)
        ↓ 볼륨 마운트 (파일 시스템 공유, 별도 전송 불필요)
[Docker 컨테이너 - Linux 빌드 환경]
  - bb-kernel로 타겟과 정확히 일치하는 커널 소스 트리 확보 (named volume에 캐싱)
  - 커널 모듈 크로스 컴파일 (arm-linux-gnueabihf-gcc, apt 설치)
  - 결과물: .ko 파일
        ↓ scp / rsync (또는 sshfs 마운트)
[BeagleBone Black]
  - insmod로 드라이버 로드
  - dmesg로 커널 로그 확인
  - 오실로스코프 등으로 실제 동작 검증
        ↓ 결과 피드백
[Mac으로 돌아가 코드 수정] → 반복
```

### Docker 빌드 환경 구성 개요

- **베이스 이미지**: `debian:bookworm` (arm64 네이티브, BBB와 동일한 Debian 버전)
- **크로스 컴파일러**: `apt install gcc-arm-linux-gnueabihf` — bb-kernel 내장 툴체인 다운로더는 x86_64 바이너리만 지원해서 Apple Silicon(arm64) Docker와 안 맞기 때문에, apt로 설치되는 네이티브 arm64용 크로스 컴파일러를 직접 지정해서 사용
- **볼륨 마운트**: macOS의 `kernel/` 디렉토리는 bind mount(코드 수정은 macOS에서, 빌드는 컨테이너 안에서), bb-kernel 소스/빌드 결과물은 named volume(`bb-kernel-cache`)에 캐싱하여 컨테이너를 재실행해도 매번 재다운로드/재빌드하지 않도록 분리
- **자동화**: `entrypoint.sh`가 컨테이너 시작 시 bb-kernel 클론 → 타겟 태그로 checkout → 빌드 → 드라이버 크로스 컴파일까지 순서대로 수행
- **BBB 연결**: 컨테이너에서 직접 BBB로 결과물을 전송(scp)하거나, macOS 호스트를 경유해서 전송

---

## 4. 소프트웨어 구조

### 전체 계층 구조

```
[ 유저 공간 — 속도 명령 입력 앱         ]
  - 속도 설정, 실시간 모니터링
  - sysfs 또는 ioctl로 드라이버와 통신
        ↕ sysfs / ioctl
[ Linux 커널 드라이버                    ]
  ├── PWM 드라이버     ← 3상 PWM 출력 (인버터 스위칭)
  ├── ADC 드라이버     ← 전류 샘플링 (FOC 피드백)
  ├── 인터럽트 핸들러  ← 엔코더 펄스 카운트 (위치/속도)
  └── FOC 알고리즘
        ├── Clarke 변환   (3상 → 2상 정지 좌표계)
        ├── Park 변환     (정지 → 회전 좌표계)
        ├── PI 제어기     (d축/q축 전류 제어)
        └── SVPWM         (전압 벡터 → PWM 출력)
        ↕ 레지스터 직접 제어
[ BeagleBone 하드웨어 + 3상 인버터 + BLDC 모터 ]
```

### FOC 알고리즘 흐름

```
전류 측정 (ADC)
  → Clarke 변환 (Ia, Ib, Ic → Iα, Iβ)
  → Park 변환 (Iα, Iβ → Id, Iq)
  → PI 제어기 (Id_ref, Iq_ref와 비교 → Vd, Vq)
  → 역Park 변환 (Vd, Vq → Vα, Vβ)
  → SVPWM (Vα, Vβ → PWM duty cycle)
  → PWM 출력 (인버터 스위칭)
  → 모터 구동
  → (반복)
```

---

## 5. 단계별 구현 계획

### 1단계 — 환경 구축 (1개월)

**목표**: 커널 드라이버를 빌드하고 타겟에 올릴 수 있는 상태

**타겟 커널 소스 확보** (Yocto 대신 택한 방식)
- BBB의 실제 커널 버전 확인 (`uname -r` → `6.12.28-bone25`)
- `apt install linux-headers-$(uname -r)` 시도 → 실패 (미니멀 커스텀 이미지라 헤더 패키지 자체가 없음)
- 대안: `RobertCNelson/bb-kernel` 빌드 프레임워크로 타겟과 정확히 일치하는 태그(`6.12.28-bone25`)를 checkout하여, 헤더 패키지 대신 쓸 완전한 커널 소스 트리를 직접 빌드

> 왜 Yocto가 아닌가: Yocto는 OS 이미지(커널+루트파일시스템) 전체를 새로 만드는 도구. 지금 필요한 건 "이미 BBB에 설치된 Debian 이미지 위에 커널 모듈 하나를 추가하는 것"뿐이라 Yocto는 과함. Yocto는 나중에 완성된 드라이버를 부팅 시 자동 로드되는 프로덕션 이미지로 패키징하고 싶을 때 다시 검토 ([6. 확장 로드맵](#6-확장-로드맵) 참조).

**Docker 크로스 컴파일 환경**
- `Dockerfile.kmod`: `debian:bookworm`(arm64) + `gcc-arm-linux-gnueabihf`
- `entrypoint.sh`: bb-kernel 클론 → 태그 checkout → 빌드 → 드라이버 컴파일 자동화
- named volume(`bb-kernel-cache`)으로 bb-kernel 빌드 결과 캐싱
- SSH로 Mac ↔ BeagleBone 연결 (USB 직결, `debian@192.168.6.1`)

**Hello World 드라이버**
- 커널 모듈(`hello_world.c`) 작성
- Makefile 작성 (KDIR을 bb-kernel이 빌드한 트리로 지정)
- insmod로 타겟에 로드 및 동작 확인
- dmesg로 커널 로그 확인

**체크포인트**
- [x] BBB SSH 접속 확인
- [x] 타겟 커널 버전과 정확히 일치하는 bb-kernel 태그 확보
- [ ] Docker 컨테이너 안에서 bb-kernel 빌드 성공
- [ ] Hello World 드라이버 insmod 성공
- [ ] dmesg에서 드라이버 로그 확인

---

### 2단계 — 드라이버 작성 (1개월)

**목표**: 하드웨어 페리페럴 전부 Linux 드라이버로 제어 가능한 상태

**Device Tree 작성**
- BeagleBone PWM, ADC, GPIO 핀 설정
- 인버터 연결 핀 매핑
- 엔코더 인터페이스 설정

**PWM 드라이버**
- Linux PWM subsystem 기반 드라이버 작성
- 3상 PWM 출력 (U, V, W 각 상)
- duty cycle 제어 인터페이스 (sysfs)
- Dead time 설정 (상하단 스위치 동시 ON 방지)

**ADC 드라이버**
- Linux IIO(Industrial I/O) subsystem 기반 드라이버 작성
- 전류 센서 샘플링
- 인터럽트 기반 샘플링 구현 (폴링 방식 대비 실시간성 개선)

**인터럽트 핸들러**
- 엔코더 펄스 카운트
- request_irq로 인터럽트 핸들러 등록
- 위치/속도 계산

**체크포인트**
- [ ] Device Tree 작성 및 적용
- [ ] PWM 핀 출력 오실로스코프로 확인
- [ ] ADC 전류 샘플링 동작 확인
- [ ] 인터럽트 기반 엔코더 카운트 동작 확인

---

### 3단계 — FOC 알고리즘 구현 (2개월)

**목표**: 모터 회전 및 속도 제어 동작 확인

**Clarke 변환 구현**
```
Iα = Ia
Iβ = (Ia + 2*Ib) / √3
```
3상 전류(Ia, Ib, Ic)를 2상 정지 좌표계(Iα, Iβ)로 변환.

**Park 변환 구현**
```
Id =  Iα*cos(θ) + Iβ*sin(θ)
Iq = -Iα*sin(θ) + Iβ*cos(θ)
```
정지 좌표계(Iα, Iβ)를 회전 좌표계(Id, Iq)로 변환. θ는 엔코더로 측정한 회전자 위치.

**PI 제어기 구현**
- d축 전류 제어 (자속 제어)
- q축 전류 제어 (토크 제어)
- 속도 제어 외부 루프 추가
- 게인(Kp, Ki) sysfs로 런타임 조정 가능하도록 구현

**SVPWM 구현**
- Vd, Vq → Vα, Vβ (역Park 변환)
- Vα, Vβ → 3상 PWM duty cycle
- 인버터 스위칭 패턴 생성

**동작 검증**
- 모터 회전 확인
- 속도 명령 추종 확인
- 부하 변동 시 응답 확인

**체크포인트**
- [ ] Clarke/Park 변환 수치 검증 (시뮬레이션 값과 비교)
- [ ] PI 제어기 동작 확인
- [ ] 모터 회전 동작 확인
- [ ] 속도 제어 동작 확인

---

### 4단계 — 검증 및 포트폴리오 정리 (1개월)

**목표**: README만 읽어도 이해 가능한 상태

**성능 측정**
- 속도 응답 시간 측정 (목표 속도 도달 시간)
- PI 게인 튜닝 및 결과 비교
- 부하 변동 시 과도 응답 특성 측정
- 전류 파형 오실로스코프로 측정

**GitHub 정리**
```
bldc-foc-linux/
├── README.md              ← 프로젝트 전체 설명
├── docs/
│   ├── architecture.md    ← 소프트웨어 계층 구조
│   ├── foc_algorithm.md   ← FOC 알고리즘 설명
│   └── results/           ← 실측 데이터, 그래프
├── yocto/                 ← (확장 단계에서만 추가) 프로덕션 이미지화 시 meta-bldc-foc 레이어
├── kernel/
│   ├── drivers/
│   │   ├── pwm_driver.c
│   │   ├── adc_driver.c
│   │   └── encoder_driver.c
│   ├── foc/
│   │   ├── clarke.c
│   │   ├── park.c
│   │   ├── pi_controller.c
│   │   └── svpwm.c
│   └── dts/               ← Device Tree 파일
└── userspace/
    └── motor_control_app.c ← 속도 명령 입력 앱
```

**README 필수 포함 내용**
- 프로젝트 목적 및 배경
- 소프트웨어 계층 구조 다이어그램
- FOC 알고리즘 설명 (수식 포함)
- 하드웨어 구성 및 회로도
- 실측 데이터 (속도 응답 그래프, 전류 파형)
- 빌드 및 실행 방법 (Docker 기반 커널 모듈 빌드 절차)

**체크포인트**
- [ ] 속도 응답 그래프 측정 완료
- [ ] GitHub README 작성 완료
- [ ] 코드 주석 정리 완료
- [ ] 빌드 방법 문서화 완료

---

## 6. 확장 로드맵

이 프로젝트는 1단계 완성 후 단계적으로 확장 가능.

### 2단계 확장 — RT Linux 적용

**목적**: 실시간성 확보 및 정량적 비교

FOC는 실시간성이 핵심인데, 일반 Linux 커널은 실시간 보장이 안 됨.
PREEMPT_RT 패치를 적용한 Real-Time Linux를 동일 하드웨어에 추가 적용.

```
일반 Linux vs RT Linux 응답 시간 실측 비교
  → 지연 시간(latency) 측정
  → jitter 측정
  → 제어 성능 비교
```

별도 하드웨어 추가 없이 소프트웨어 변경만으로 확장 가능.

### 3단계 확장 — CAN 인터페이스 추가

**목적**: 자동차 도메인 연결, 지원 범위 확대

```
[ CAN으로 속도/토크 명령 수신 ]
[ SocketCAN 드라이버 추가     ]
[ 기존 FOC 제어 시스템        ]
```

모터 제어 명령을 CAN 메시지로 수신하도록 확장.
SocketCAN은 Linux 커널 내장 CAN 프레임워크.
자동차 제어기 개발 경험(CAN 실무 사용)과 직접 연결되는 확장.

### 4단계 확장 — 실차 연동 (최종)

**목적**: 회사 경험과 프로젝트 연결

```
[ 회사에서 만든 실차 로그 검증 프로그램 ]
        +
[ CAN 데이터 로거로 모터 상태 기록       ]
```

회사에서 개발한 실차 로그 기반 검증 프로그램 경험과 합쳐지는 지점.

### 확장 — Yocto 기반 프로덕션 이미지화

**목적**: 완성된 드라이버를 "매번 수동으로 insmod"하는 개발용 상태에서, 부팅 시 자동으로 로드되는 완성된 제품 이미지로 패키징

```
[ 지금까지 만든 드라이버 + bb-kernel 커널 설정 ]
        +
[ meta-bldc-foc 커스텀 Yocto 레이어 ]
        ↓
[ 부팅 시 드라이버 자동 로드되는 SD카드 이미지 ]
```

- 지금까지 확정한 커널 버전/설정(`bb-kernel`의 `system.sh`, 매칭 태그)과 out-of-tree 모듈 빌드 지식을 그대로 재사용해서, `meta-bldc-foc` 레이어 안에 드라이버를 빌드하는 `.bb` 레시피를 추가
- 드라이버 코드 자체가 안정화된 뒤에 진행하는 게 맞음 — Yocto는 이미지 최초 빌드에만 수 시간이 걸려서, 드라이버를 계속 수정/테스트하는 개발 단계에서 같이 하면 반복 주기가 크게 느려짐
- 별도 하드웨어 추가 없이 소프트웨어 변경만으로 확장 가능

**스코프를 의도적으로 넓힐 것** (그냥 레시피 하나 추가로 끝내지 않기 위함):
- 기존 `meta-ti`/`meta-beagleboard` BSP를 그대로 쓰지 않고, 커스텀 머신/디스트로 설정을 일부 직접 구성
- 이미지 크기/패키지 선택 최적화 (프로덕션에 불필요한 패키지 제거)
- 라이선스 매니페스트 생성 등 실무형 태스크 포함
- 이렇게 해야 "Yocto 레시피 하나 추가해봄" 수준이 아니라 실제 BSP 운영 경험에 가까워짐

---

## 7. 이 프로젝트로 쌓는 기술 스택

### Linux 시스템

| 기술 | 설명 |
|---|---|
| Yocto | 임베디드 Linux 이미지 빌드 시스템 (확장 단계에서 프로덕션 이미지화 시 사용 예정, 현재 미사용) |
| 크로스 컴파일 | 타겟 아키텍처(ARM)용 빌드 환경 |
| Device Tree | 하드웨어 구성 기술 언어 (DTS/DTB) |
| Linux 커널 모듈 | insmod/rmmod, Kbuild 시스템 |

### Linux 커널 드라이버

| 기술 | 설명 |
|---|---|
| Character Device Driver | open/read/write/ioctl 구현 |
| PWM subsystem | 커널 PWM 프레임워크 기반 드라이버 |
| IIO subsystem | Industrial I/O, ADC 드라이버 |
| 인터럽트 처리 | request_irq, 인터럽트 핸들러 작성 |
| 커널-유저 공간 통신 | sysfs, ioctl |
| ioremap | 물리 주소를 커널 가상 주소로 매핑 |

### 제어 알고리즘

| 기술 | 설명 |
|---|---|
| Clarke/Park 변환 | 좌표계 변환 |
| PI 제어기 | 전류/속도 제어 |
| SVPWM | 공간 벡터 PWM |
| FOC | Field Oriented Control 전체 구조 |

### 개발 도구

| 기술 | 설명 |
|---|---|
| GDB + JTAG | 커널 레벨 디버깅 |
| 시리얼 콘솔 | 부팅 로그, 커널 패닉 분석 |
| 오실로스코프 | PWM 파형, 전류 파형 측정 |
| Git/GitHub | 버전 관리, 포트폴리오 |

### 직무 대응

| 직무 요구 | 이 프로젝트로 쌓는 것 |
|---|---|
| Linux Kernel BSP Driver | PWM/IIO/인터럽트 드라이버, Device Tree |
| Linux Framework | sysfs 인터페이스, 커널 모듈 구조 |
| Platform 개발 | 유저 공간 모터 제어 앱 |

---

## 8. 총 일정

| 단계 | 기간 | 핵심 결과물 |
|---|---|---|
| 1단계 환경 구축 | 1개월 | Docker+bb-kernel 빌드 환경, Hello World 드라이버 |
| 2단계 드라이버 작성 | 1개월 | PWM/ADC/인터럽트 드라이버, Device Tree |
| 3단계 FOC 구현 | 2개월 | 모터 회전 및 속도 제어 동작 |
| 4단계 정리 | 1개월 | GitHub README, 실측 데이터 |
| **합계** | **5~6개월** | **동작하는 결과물 + 문서화된 GitHub** |

---

## 9. 참고

**이 문서 작성 시점**
- 사이드 프로젝트 착수 전 계획 단계
- 1단계부터 순서대로 진행 예정
- 막히는 단계에서 이 문서를 첨부하여 Claude에게 질문

**문서 갱신 이력**
- v1: 최초 작성 (계획 단계)
- v2: 개발 환경을 Mac 단독 빌드에서 Docker 기반 빌드로 변경. macOS(코드 작성) / Docker 컨테이너(Yocto·커널 모듈 빌드) / BeagleBone Black(실행·테스트) 3단 역할 분리 원칙 명시
- v3: Yocto를 1단계에서 제외. 실제로 BBB에 접속해보니 기본 제공 Debian 이미지 위에 커널 모듈만 얹으면 충분함을 확인하여, `bb-kernel` 기반 매칭 커널 소스 확보 + Docker(`debian:bookworm` arm64 + `gcc-arm-linux-gnueabihf`) 크로스 컴파일 방식으로 전환. Yocto는 드라이버 완성 후 프로덕션 이미지화가 필요할 때 쓰는 확장 단계로 이동
- v4: Yocto 전환 순서에 대한 판단 근거를 명시적으로 기록. 지금 바로 Yocto를 도입해도 배경 지식 부족으로 이해하기 어렵다고 판단하여, 먼저 Docker+bb-kernel 시스템을 동작시켜 커널/드라이버 이해도를 쌓은 뒤 Yocto 전환 여부를 그때 다시 판단하기로 결정. 레시피 하나만 얹는 얕은 도입이 되지 않도록 확장 시 스코프 확대 계획도 함께 기록