#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# ARM_PREFIX may include the absolute toolchain directory.
arm_tools="${ARM_PREFIX:-arm-none-eabi-}"
python3 gen_assets.py
"${arm_tools}gcc" -mcpu=cortex-m0plus -mthumb -Os -flto -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -Wl,--gc-sections,-Map=fmorse.map -Wl,--no-warn-rwx-segments -T app.ld fmorse_app.c -lgcc -o fmorse.elf
"${arm_tools}objcopy" -O binary fmorse.elf fmorse.bin
"${arm_tools}size" fmorse.elf
python3 ../pack_app.py fmorse.bin FMorse.app --name FMorse --ver 1.7 --vma 0x20000280 --api-min 2 --assets morse_assets.bin
