#!/usr/bin/env bash
# One-command release build, quiet: only a summary is printed, the details go
# to build/release/release.log.
#   1. Falcon 1K and 4K one-file BINs (firmware/f4hwn.falcon-1k|4k.v<version>.bin),
#      with each installer's margin
#   2. every Falcon app, copied to Apps/Falcon apps (translations included)
#   3. the emulation tests, 8 at a time, failures retried once alone
#      (4K: runtime + installer tests; 1K: runtime tests)
#   4. Falcon-apps-v<version>.zip in build/release/
#   5. SHA256SUMS over the tracked files (git add new files first)
# Options: --no-tests, --no-apps, --no-firmware (keep the BINs already in
# firmware/), --4k (4K only), --sums-only.
set -uo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/falcon-toolchain/gcc/bin:$HOME/falcon-toolchain/venv/bin:$PATH"
OUT=build/release
LOG=$OUT/release.log
mkdir -p "$OUT" firmware
: >"$LOG"
TESTS=1 APPS=1 FIRMWARE=1 EDITIONS="1K 4K" SUMS_ONLY=0
for a in "$@"; do
    case "$a" in
        --no-tests) TESTS=0 ;;
        --no-apps) APPS=0 ;;
        --no-firmware) FIRMWARE=0 ;;
        --4k) EDITIONS="4K" ;;
        --sums-only) SUMS_ONLY=1 ;;
        *) echo "unknown option $a"; exit 2 ;;
    esac
done
VERSION=$(python3 -c "import json;print(json.load(open('sources/runtime/CMakePresets.json'))['configurePresets'][0]['cacheVariables']['VERSION_STRING_2'])")
FAIL=0
step() { printf '%-40s' "$1"; }
ok() { echo "${1:-OK}"; }
ko() { echo "FAILED (see $LOG)"; FAIL=1; }

sums() {
    step "SHA256SUMS"
    git ls-files | grep -v -e '^\.gitignore$' -e '^SHA256SUMS$' | while IFS= read -r f; do [ -f "$f" ] && printf '%s\n' "$f"; done |
        sort | tr '\n' '\0' | xargs -0 shasum -a 256 >SHA256SUMS 2>>"$LOG" \
        && ok "$(wc -l <SHA256SUMS | tr -d ' ') files" || ko
    local new
    new=$(git ls-files --others --exclude-standard firmware chirp docs "Apps/Falcon apps" | head -5)
    [ -n "$new" ] && echo "  not tracked yet (git add, then --sums-only):" && echo "$new" | sed 's/^/    /'
}
[ "$SUMS_ONLY" = 1 ] && { sums; exit $FAIL; }

echo "Falcon v$VERSION"
# 1. Firmware: the installer of the last edition built is the 4K one (tests).
for E in $EDITIONS; do
    [ "$FIRMWARE" = 1 ] || break
    step "Falcon $E firmware"
    if { cmake --preset "S$E" -S sources/runtime -B "sources/runtime/build/S$E" -DTARGET=falcon.data &&
         cmake --build "sources/runtime/build/S$E" -j &&
         python3 tools/build_payload.py sources/runtime sources/installer --name falcon.data --preset "S$E" &&
         cmake --preset S4K -S sources/installer -B sources/installer/build/S4K -DTARGET=falcon.installer \
             "-DINSTALL_EDITION=Falcon $E" &&
         cmake --build sources/installer/build/S4K -j; } >>"$LOG" 2>&1; then
        # UV Studio names a multiboot slot from the file name: "Falcon 4k" / "Falcon 1k"
        bin="firmware/f4hwn.falcon-$(echo "$E" | tr K k).v${VERSION}.bin"
        cp sources/installer/build/S4K/falcon.installer.bin "$bin"
        used=$(grep -a "FLASH:" "$LOG" | tail -1 | awk '{print $2}')
        ok "$(stat -f%z "$bin") B, installer margin $((120832 - used - 924)) B"
    else
        ko
    fi
done

# 2. Apps.
if [ "$APPS" = 1 ]; then
    step "Falcon apps"
    n=0
    # POCSAG Scan builds with make.sh.
    for b in sources/*/*/build.sh sources/pocsagrx/pocsagrx/make.sh; do
        case "$b" in sources/runtime/* | sources/installer/*) continue ;; esac
        d=$(dirname "$b")
        langs=en
        ls "$d"/content_*.py >/dev/null 2>&1 && langs="en $(ls "$d"/content_*.py | sed 's/.*content_\(.*\)\.py/\1/' | tr '\n' ' ')"
        for L in $langs; do
            if (cd "$d" && bash "$(basename "$b")" "$L") >>"$LOG" 2>&1; then n=$((n + 1)); else FAIL=1; echo "  $d $L failed"; fi
        done
        for app in "$d"/*.app; do
            name=$(basename "$app")
            case "$name" in
                *-[A-Z][A-Z].app) lang=${name%.app}; lang=${lang##*-}; mkdir -p "Apps/Falcon apps/Translations/$lang"
                    cp "$app" "Apps/Falcon apps/Translations/$lang/" ;;
                *) cp "$app" "Apps/Falcon apps/" ;;
            esac
        done
    done
    ok "$n builds, copied to Apps/Falcon apps"
    step "Falcon-apps-v$VERSION.zip"
    rm -f "$OUT/Falcon-apps-v$VERSION.zip"
    (cd Apps && zip -qr "../$OUT/Falcon-apps-v$VERSION.zip" "Falcon apps") && ok || ko
fi

# 3. Tests, per edition (each against its own build).
run_tests() { # $1 edition, $2.. test files
    local E=$1; shift
    export FALCON_TEST_BUILD="$PWD/sources/runtime/build/S$E"
    printf '%s\n' "$@" | xargs -P 8 -I{} sh -c 'perl -e "alarm 900; exec @ARGV" python3 "{}" >/dev/null 2>&1 || echo "{}"' >"$OUT/failed_$E.txt"
    local retried=""
    while read -r t; do
        [ -z "$t" ] && continue
        out=$(perl -e "alarm 900; exec @ARGV" python3 "$t" 2>&1) || { retried="$retried $(basename "$t")"; echo "== $t" >>"$LOG"; echo "$out" | tail -20 >>"$LOG"; }
    done <"$OUT/failed_$E.txt"
    echo "$retried"
}
if [ "$TESTS" = 1 ]; then
    for E in $EDITIONS; do
        step "Tests $E"
        files=$(ls sources/runtime/tools/tests/test_*.py | grep -v test_firstaid_arm)
        [ "$E" = 4K ] && files="$files $(ls validation/tests/test_*.py)"
        [ "$E" = 4K ] && files=$(echo $files | tr ' ' '\n' | grep -v chirp_1k)
        start=$(date +%s)
        bad=$(run_tests "$E" $files)
        count=$(echo $files | wc -w | tr -d ' ')
        if [ -z "${bad// /}" ]; then ok "$count passed in $(($(date +%s) - start)) s"; else echo "FAILED:$bad"; FAIL=1; fi
    done
    git checkout -- validation/installer-preview.png 2>/dev/null
fi

# 4. Checksums last.
sums
exit $FAIL
