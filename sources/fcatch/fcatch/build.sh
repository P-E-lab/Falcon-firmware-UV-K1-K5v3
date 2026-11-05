#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 make_assets.py fcatch_assets.bin
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -fno-jump-tables -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections -Wl,-Map=fcatch.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments fcatch_app.c -o fcatch.elf
arm-none-eabi-objcopy -O binary fcatch.elf fcatch.bin
arm-none-eabi-size fcatch.elf
python3 ../pack_app.py fcatch.bin FCatch.app --name "FCatch" --ver "1.6" --api-min 3 --vma 0x20000280 --require beam2 --exit-main --assets fcatch_assets.bin
