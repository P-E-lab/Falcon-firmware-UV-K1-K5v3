#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 make_text.py fguide_text.bin text_tail.h 3840
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -fno-jump-tables -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections -Wl,-Map=fguide.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments fguide_app.c -o fguide.elf
arm-none-eabi-objcopy -O binary fguide.elf fguide.bin
python3 ../pack_app.py fguide.bin FGuide.app --name "FGuide" --ver "1.0" --api-min 3 --vma 0x20000280 --screensaver --assets fguide_text.bin
