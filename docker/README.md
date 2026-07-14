# Docker 기반 빌드 환경 운용 컨셉

BBB(BeagleBone Black) 모터 제어 프로젝트의 크로스 컴파일 환경을 Docker로 운용한다.
호스트(macOS)에는 ARM 툴체인을 설치하지 않고, 모든 빌드는 컨테이너 안에서 수행한다.

## 1. 전체 구조

```
BLDC/
├── docker/
│   ├── Dockerfile.kmod    # 커널 모듈 빌드용 이미지 정의
│   ├── build.sh           # 이미지 빌드 + 컨테이너 실행 (호스트에서 실행)
│   ├── entrypoint.sh      # 컨테이너 시작 시 자동 실행되는 빌드 스크립트
│   └── README.md          # 본 문서
├── kernel/
│   ├── drivers/           # 커널 모듈 소스 (ADC, 인터럽트 등 MCU 제어 로직)
│   │   └── hello_world/   # 드라이버 예제 (Makefile + .c)
│   ├── dts/               # Device Tree 소스
│   └── bbb-config/        # 커널 config (config-6.12.28-bone25)
└── application/           # 유저스페이스 FOC 알고리즘 (추후 직접 작성)
```

## 2. 이미지 분리 원칙

**커널 모듈 빌드용 이미지와 애플리케이션 빌드용 이미지는 분리한다.**

| | 커널 모듈 (kmod) | 애플리케이션 (app, 추후) |
|---|---|---|
| Dockerfile | `Dockerfile.kmod` | `Dockerfile.app` (예정) |
| 빌드 대상 | `kernel/drivers/*` → `.ko` | `application/*` → ARM 실행 파일 |
| 필요 환경 | bb-kernel 전체 소스 트리 + 커널 빌드 의존성 (bc, bison, flex, libssl-dev 등) | `arm-linux-gnueabihf-gcc` + libm 정도의 경량 환경 |
| 반복 주기 | 느리고 가끔 (드라이버 안정화 후 거의 고정) | 빠르고 자주 (FOC 알고리즘 튜닝) |

분리 이유:
- 두 빌드의 툴체인·의존성·반복 주기가 다르다. 합치면 앱 코드 한 줄 수정에도 무거운 커널 빌드 환경을 안고 가야 한다.
- 이미지 하나 = 책임 하나. kmod 이미지는 커널 모듈 빌드만 책임진다.

## 3. 소스는 COPY가 아닌 볼륨 마운트로 주입

이미지에는 **툴체인만** 들어 있고, 소스는 `docker run -v`로 마운트한다.

```bash
docker run --rm \
    -v "${PROJECT_ROOT}/kernel:/workspace/kernel" \   # 호스트 kernel/ → 컨테이너 /workspace/kernel
    -v bb-kernel-cache:/opt/bb-kernel \               # bb-kernel 소스 트리 캐시 (named volume)
    -e TARGET_KERNEL_TAG=6.12.28-bone25 \
    bldc-kmod-builder
```

- 소스를 수정해도 **이미지 재빌드가 필요 없다.** 마운트라 즉시 반영된다.
- 빌드 산출물(`.ko`)도 마운트를 통해 호스트에 바로 남는다.
- `bb-kernel-cache`는 named volume이다. 최초 실행 시 bb-kernel 클론 + 전체 커널 빌드(오래 걸림)를 수행하고, 이후에는 캐시된 트리를 재사용해 `rebuild.sh`만 돈다.

### 마운트 범위 = 최소 필요 범위

프로젝트 루트 전체가 아니라 `kernel/`만 마운트한다.
- `.git/`, `application/` 등 커널 모듈 빌드와 무관한 것을 컨테이너에 노출하지 않는다.
- 마운트 범위가 곧 부작용 범위다. 컨테이너 안의 서드파티 스크립트(bb-kernel)가 잘못 동작해도 피해가 `kernel/`로 제한된다.

## 4. entrypoint.sh 빌드 흐름

컨테이너 시작 시 자동으로 다음을 수행한다:

1. `/opt/bb-kernel`에 [RobertCNelson/bb-kernel](https://github.com/RobertCNelson/bb-kernel)이 없으면 클론 (캐시 볼륨이라 최초 1회만)
2. `TARGET_KERNEL_TAG` (현재 `6.12.28-bone25`)로 checkout
3. `system.sh` 생성 (`CC=arm-linux-gnueabihf-`, `AUTO_BUILD=1`)
4. `KERNEL/` 트리가 없으면 `build_kernel.sh` (최초, 오래 걸림) / 있으면 `tools/rebuild.sh` (증분)
5. 준비된 커널 트리를 KDIR 삼아 드라이버를 out-of-tree 빌드:

```bash
make -C "${BB_KERNEL_DIR}/KERNEL" M="${DRIVER_SRC}" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
```

- `-C <KDIR>`: 빌드 설정이 완료된 커널 소스 트리에서 kbuild 실행
- `M=<드라이버 경로>`: 커널 트리 밖(out-of-tree)의 외부 모듈 소스 위치 지정
- `ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-`: BBB(ARM 32bit hard-float) 타겟 크로스 컴파일
- `modules`: `.ko` 커널 모듈만 빌드

## 5. 새 소스 추가 시 수정 지점

### 케이스 A: 기존 드라이버 폴더에 `.c` 파일 추가
Docker 파일 수정 불필요. 해당 드라이버의 `Makefile`만 수정:
```makefile
obj-m += my_driver.o
my_driver-objs := main.o adc.o irq.o   # 여러 .c를 하나의 .ko로
```

### 케이스 B: `kernel/drivers/` 아래 새 드라이버 폴더 추가 (예: adc, irq 드라이버)
마운트는 자동으로 되지만 `entrypoint.sh`의 `DRIVER_SRC`가 하드코딩되어 있으므로 빌드 대상에 추가해야 한다:
```bash
for DRIVER_SRC in /workspace/kernel/drivers/*/; do
    make -C "${BB_KERNEL_DIR}/KERNEL" M="${DRIVER_SRC}" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
done
```

### 케이스 C: `kernel/` 밖에 새 최상위 경로 추가
`build.sh`에 `-v` 마운트 추가 + `entrypoint.sh`에 빌드 대상 추가.

### 케이스 D: 새 소스가 추가 패키지를 요구
`Dockerfile.kmod`의 `apt-get install` 목록에 패키지 추가 후 이미지 재빌드.

## 6. 사용법

```bash
cd BLDC/docker
./build.sh    # 이미지 빌드 + 컨테이너 실행 + 드라이버 빌드까지 한 번에
```

- 최초 실행: bb-kernel 클론 + 전체 커널 빌드로 수십 분 소요
- 이후 실행: 캐시 재사용, 드라이버 증분 빌드만 수행
- 산출물: `kernel/drivers/<드라이버>/<이름>.ko` (호스트에서 바로 확인 가능)

캐시 초기화가 필요할 때 (커널 태그 변경, 트리 오염 등):
```bash
docker volume rm bb-kernel-cache
```

## 7. 향후 계획 (application 빌드)

- `application/`에 FOC 모터 제어 알고리즘을 C로 작성 (직접 작성 예정)
- `Dockerfile.app` 신설: `gcc-arm-linux-gnueabihf` 기반 경량 이미지
- `application/`만 마운트, 드라이버가 제공하는 인터페이스(`/dev/*`, sysfs 등)를 통해 커널 모듈과 통신
- 빌드 스크립트도 `build_app.sh` 등으로 분리해 kmod 빌드와 독립적으로 운용