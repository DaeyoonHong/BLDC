#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

docker build -t bldc-kmod-builder -f "${SCRIPT_DIR}/Dockerfile.kmod" "${SCRIPT_DIR}"

docker run --rm \
    -v "${PROJECT_ROOT}/kernel:/workspace/kernel" \
    -v bb-kernel-cache:/opt/bb-kernel \
    -e TARGET_KERNEL_TAG=6.12.28-bone25 \
    bldc-kmod-builder
