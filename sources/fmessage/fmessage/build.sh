#!/usr/bin/env bash
# Build FMessage.app (needs arm-none-eabi-gcc and python3).
set -euo pipefail
cd "$(dirname "$0")"
APP_NAME="FMessage"; APP_VER="3.0"; APP_VMA=0x20000280
CFLAGS="-mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common \
  -fomit-frame-pointer -fno-jump-tables -fno-inline-small-functions -fno-move-loop-invariants -fno-tree-slsr -fno-tree-dominator-opts -fno-strict-aliasing -fno-tree-forwprop -fno-tree-sink -fno-code-hoisting -fno-tree-loop-im -fno-cse-follow-jumps -ffunction-sections -fdata-sections -Wall -Wextra"
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} -Wl,--gc-sections \
  -Wl,-Map=fmessage.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"
python3 gen_assets.py fmessage_assets.bin fmessage_assets.h
arm-none-eabi-gcc $CFLAGS $LDFLAGS fmessage_app.c -lgcc -o fmessage.elf
arm-none-eabi-objcopy -O binary fmessage.elf fmessage.bin
python3 ../pack_app.py fmessage.bin "${APP_NAME}.app" --name "$APP_NAME" --ver "$APP_VER" --shortcut fmessage \
  --api-min 3 --vma "$APP_VMA" --assets fmessage_assets.bin >/dev/null
BYTES=$(wc -c < fmessage.bin)
echo "FMessage: code ${BYTES} B / 4096 ($(( BYTES * 100 / 4096 ))%), assets $(wc -c < fmessage_assets.bin) B / 3840"
