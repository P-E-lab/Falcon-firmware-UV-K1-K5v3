#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
arm_tools="${ARM_PREFIX:-arm-none-eabi-}"
python3 gen_assets.py snake_assets.bin snake_assets.h
"${arm_tools}gcc" -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections,-Map=fsnake.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments fsnake_app.c -lgcc -o fsnake.elf
"${arm_tools}objcopy" -O binary fsnake.elf fsnake.bin
python3 ../pack_app.py fsnake.bin FSnake.app --name FSnake --ver 1.6 --api-min 2 --vma 0x20000280 --screensaver --assets snake_assets.bin
