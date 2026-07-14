#!/bin/bash
set -e

BB_KERNEL_DIR=/opt/bb-kernel
DRIVERS_ROOT=/workspace/kernel/drivers

if [ ! -d "${BB_KERNEL_DIR}/.git" ]; then
    echo "[entrypoint] bb-kernel 클론 중..."
    git clone https://github.com/RobertCNelson/bb-kernel "${BB_KERNEL_DIR}"
fi

cd "${BB_KERNEL_DIR}"
git checkout "${TARGET_KERNEL_TAG}"

if [ ! -f system.sh ]; then
    echo "CC=/usr/bin/arm-linux-gnueabihf-" > system.sh
    echo "AUTO_BUILD=1" >> system.sh
fi

if [ ! -d KERNEL ]; then
    echo "[entrypoint] 최초 빌드: build_kernel.sh 실행"
    ./build_kernel.sh
else
    echo "[entrypoint] 기존 트리 존재: tools/rebuild.sh 실행"
    ./tools/rebuild.sh
fi

echo "[entrypoint] KDIR=${BB_KERNEL_DIR}/KERNEL 로 드라이버 빌드"
for driver_dir in "${DRIVERS_ROOT}"/*/; do
    if [ -f "${driver_dir}/Makefile" ]; then
        echo "[entrypoint] 드라이버 빌드: ${driver_dir}"
        make -C "${BB_KERNEL_DIR}/KERNEL" M="${driver_dir}" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
    fi
done
