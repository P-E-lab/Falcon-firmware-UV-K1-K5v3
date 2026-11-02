#!/usr/bin/env bash
# Build FMessageOption.app (needs arm-none-eabi-gcc and python3).
set -euo pipefail
cd "$(dirname "$0")"
APP_NAME="FMessOption"; APP_VER="3.0"; APP_VMA=0x20000280
CFLAGS="-mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common \
  -fomit-frame-pointer -fno-jump-tables -fno-inline-small-functions -fno-move-loop-invariants -ffunction-sections -fdata-sections -Wall -Wextra"
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} -Wl,--gc-sections \
  -Wl,-Map=fmsgoption.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"
python3 gen_assets.py fmsgoption_assets.bin fmsgoption_assets.h
arm-none-eabi-gcc $CFLAGS $LDFLAGS fmsgoption_app.c -lgcc -o fmsgoption.elf
arm-none-eabi-objcopy -O binary fmsgoption.elf fmsgoption.bin
python3 ../pack_app.py fmsgoption.bin "FMessageOption.app" --name "$APP_NAME" --ver "$APP_VER" \
  --api-min 3 --vma "$APP_VMA" --require cfgproxy --assets fmsgoption_assets.bin >/dev/null
BYTES=$(wc -c < fmsgoption.bin)
echo "FMessage Option: code ${BYTES} B / 4096 ($(( BYTES * 100 / 4096 ))%), assets $(wc -c < fmsgoption_assets.bin) B / 3840"
