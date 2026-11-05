#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Language: en (default), fr, es... (content.py or content_<lang>.py).
LANG_ID=${1:-en}
OUT=FSurvival.app
VER=1.0
if [ "$LANG_ID" != en ]; then OUT=FSurvival-$(echo "$LANG_ID" | tr a-z A-Z).app; VER=1.0-$(echo "$LANG_ID" | tr a-z A-Z); fi
python3 make_text.py fsurvival_text.bin text_tail.h 3840 "$LANG_ID"
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -fno-jump-tables -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections -Wl,-Map=fsurvival.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments fsurvival_app.c -o fsurvival.elf
arm-none-eabi-objcopy -O binary fsurvival.elf fsurvival.bin
python3 ../pack_app.py fsurvival.bin "$OUT" --name "FSurvival" --ver "$VER" --api-min 3 --vma 0x20000280 --assets fsurvival_text.bin
