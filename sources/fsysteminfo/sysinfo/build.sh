#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 gen_assets.py sysinfo_assets.bin sysinfo_assets.h
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra -DENABLE_FEAT_F4HWN_OVERLAY_INFO -nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=0x20000280 -Wl,--gc-sections -Wl,-Map=sysinfo.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments sysinfo_app.c -lgcc -o sysinfo.elf
arm-none-eabi-objcopy -O binary sysinfo.elf sysinfo.bin
python3 ../pack_app.py sysinfo.bin FSystemInfo.app --name 'FSystemInfo' --ver 3.0.0-OA --api-min 2 --vma 0x20000280 --screensaver --assets sysinfo_assets.bin --require sysinfo
