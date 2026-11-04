#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 make_text.py firstaid_text.bin text_tail.h 3840
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -fno-jump-tables -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections -Wl,-Map=firstaid.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments firstaid_app.c -o firstaid.elf
arm-none-eabi-objcopy -O binary firstaid.elf firstaid.bin
python3 ../pack_app.py firstaid.bin FirstAid.app --name "FirstAid" --ver "1.0" --api-min 3 --vma 0x20000280 --assets firstaid_text.bin
