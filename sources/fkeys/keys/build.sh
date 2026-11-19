#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -fno-jump-tables -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections -Wl,-Map=fkeys.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments fkeys_app.c -o fkeys.elf
arm-none-eabi-objcopy -O binary fkeys.elf fkeys.bin
python3 ../pack_app.py fkeys.bin FKeys.app --name "FKeys" --ver "4.0.0" --api-min 3 --vma 0x20000280 --screensaver
