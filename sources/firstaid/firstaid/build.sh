#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Language: en (default), fr, es... (content.py or content_<lang>.py).
LANG_ID=${1:-en}
OUT=FirstAid.app
VER=1.0
if [ "$LANG_ID" != en ]; then OUT=FirstAid-$(echo "$LANG_ID" | tr a-z A-Z).app; VER=1.0-$(echo "$LANG_ID" | tr a-z A-Z); fi
python3 make_text.py firstaid_text.bin text_tail.h 3840 "$LANG_ID"
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -fno-jump-tables -ffunction-sections -fdata-sections -Wall -Wextra -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections -Wl,-Map=firstaid.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments firstaid_app.c -o firstaid.elf
arm-none-eabi-objcopy -O binary firstaid.elf firstaid.bin
python3 ../pack_app.py firstaid.bin "$OUT" --name "FirstAid" --ver "$VER" --api-min 3 --vma 0x20000280 --assets firstaid_text.bin
