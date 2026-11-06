# Falcon v3.1.1-OA

**English** | [Français](README.fr.md) | [Español](README.es.md)

Firmware for UV-K1 / K5(v3), derived from Sonic and compatible with F4HWN (64 app slots).

- Clear screens, Keys Infos help on F (#), and two scan speeds: Normal for reliable detection (~300 channels/s) and FastScan to sweep many channels quickly (~500 channels/s).
- Full Watch follows both VFOs and up to four priority channels; Multilist Watch scans your memory lists in the background.
- Scan straight from the VFO: Long * scans the channel's list, F + * scans all channels in FastScan, and with A and B in frequency mode, Long * scans the range between them, keeping the VFO display.
- Spectrum and Sentinel scan lists, bands or ranges, with a History of up to 200 detections and blacklists.
- Channels, lists and bands can be created and edited directly from the keypad.
- Falcon apps: FMessage sends and receives 64-character FSK text messages between radios; messages are saved on the radio, accessible from the VFO (MESSAGES shortcut) or with the FMessage app (VFO shortcut also available). It sends a call with a melody and shows which radios are present with the app open (melody and callsign set in FMessageOption); each radio that acknowledged the last message is marked with a +. FCatch (Close Call capture) for VHF and UHF signals, FirstAid, FSurvival and FMorse for emergency situations, FGuide (firmware keys and shortcuts guide), FSystemInfo, and FSnake (open walls, keys 3/7 for both directions) complete the F4HWN catalogue.
- One-file install, multiboot with Sonic and F4HWN (apps supported), and two editions: Falcon 4K (4096 channels) and Falcon 1K (1024 channels with their own settings).

**User guide (EN): [docs/Falcon_v3.1.1_User_Guide_EN.pdf](docs/Falcon_v3.1.1_User_Guide_EN.pdf)**. The guide of each version is also attached to the [Releases](../../releases).

## Contents

- `firmware/Falcon4K_v3.1.1-OA.bin`: firmware and all-in-one automatic installer (3.1.1: an app can save a block of its own data, used by FCatch).
- `firmware/Falcon1K_v3.1.1-OA.bin`: Falcon 1K, the same version with 1024 channels kept in its own slot's settings (F4HWN layout). In multiboot with Sonic 4K, each one keeps its own settings and channels. Falcon 4K shares its 4096 channels with Sonic 4K.
- `Falcon apps/FGuide.app`: Falcon key reference on the radio (10 sections, in English).
- `Falcon apps/FSystemInfo.app`: Falcon system information (firmware, memory, stack, battery, identity).
- `Falcon apps/FCatch.app`: Close Call capture of nearby transmitters, with tone, memory channel name, VHF / UHF / VHF+UHF choice, frequency blocking (SK2) and a saved 17-entry history (Falcon 3.1.1 or later).
- `Falcon apps/FirstAid.app`: first aid steps (ERC 2025) and CPR metronome.
- `Falcon apps/FSurvival.app`: survival tips and SOS torch beacon.
- `Falcon apps/FMorse.app`: Morse messages by light or radio (formerly Morse).
- `Falcon apps/FSnake.app`: Snake game (formerly Snake F).
- `Falcon apps/Translations`: FirstAid and FSurvival in French (FR), Spanish (ES) and Polish (PL).
- `Falcon apps/FMessage.app` and `Falcon apps/FMessageOption.app`: messaging between radios (FMessage 3.0) and its settings ("FMessOption" in the apps list). Install both.
- `F4WHN apps`: the 46 official F4HWN apps available (version 6.3.0), optional.
- `chirp/falcon4k.chirp.v3.1.1.py`: Falcon 4K CHIRP driver (formerly `falcon.chirp.v3.1.0.py`), with the four priority channels, SetRxG and the REVERSE FQ, MESSAGES and FMESSAGE shortcuts.
- `chirp/falcon1k.chirp.v3.1.1.py`: Falcon 1K CHIRP driver (memories 995 to 1024: the bands). Each driver refuses the other edition.
- `sources/runtime`: complete firmware sources, drivers and embedded dependencies.
- `sources/installer`: sources of the temporary installer (it reuses the drivers of `sources/runtime`).
- `sources/fguide`, `sources/fsysteminfo`: sources of FGuide and FSystemInfo.
- `sources/fcatch`: sources of FCatch (capture and tones derived from FSearch / GOGUFW Search, Apache-2.0).
- `sources/firstaid`, `sources/fsurvival`, `sources/fmorse`, `sources/fsnake`: sources of FirstAid, FSurvival (with their translations), FMorse and FSnake.
- `sources/fmessage`: sources and tests of FMessage and FMessOption.
- `tools`, `validation/tests`, `Dockerfile`, `build-with-docker.sh`: build and tests.
- `LICENSE`, `NOTICE`: licenses and attributions. Third-party sources keep their notices.

## Installation

Use a UVStudio version that supports the 64 app slots.

To keep the main firmware: boot that firmware, open Firmware Slots in UVStudio, write the BIN to a free slot 1 to 4, then restart while holding MENU to choose that slot.

To replace the main firmware: use Firmware Flash / DFU with the same BIN.

At first start, let the "FALCON v3.1.1" and "Powering Up" install screen finish its work until the automatic restart. Do not cut the power during the install; an interrupted install is resumed at the next start, in the same slot. No "Install assets" click is needed.

If the install screen shows `S 04 E 10`, the install was refused without erasing anything: the Falcon area holds apps written by another firmware (app slots 80 to 99 of a future F4HWN). Press PTT to restore the main firmware.

If Falcon shows "MODULES MISSING / REINSTALL FALCON", its modules in external memory were erased or replaced: write the same BIN again.

The apps (those of `Falcon apps` and `F4WHN apps`) are installed separately from the Apps tab of UVStudio, as you choose. They start from the apps menu (F+5) in Falcon.

## Memory layout

- Internal flash: the core of the firmware.
- External memory, per firmware slot: the code modules (loaded on demand into 4 KB of RAM, DMA read and checked by the hardware CRC; a module can write the flash or call another module, and it is reloaded before returning to it) and the resources (fonts, menu texts). They use the slot's v7 asset zone (0x1CA000 + slot × 16 KB) and a 32 KB bank at the end of the v7 Apps reserve (0x1A2000 + slot × 32 KB, app slots 80 to 99). The 64 app slots, the configuration, the calibration and the channels are never touched. See `sources/runtime/App/store_layout.h`.
- The display only receives the changed lines, with a complete frame at least once per second.

## How this version works

Full Watch (RX Mode menu "FULL WATCH") watches both VFOs and up to four priority channels, with a 100 ms time slot per position. A position is left earlier (from 50 ms) when two successive measurements of the received level are clearly below the squelch opening threshold (6 dB margin); near or above the threshold, the full 100 ms are kept. Ordinary Dual keeps its 100 ms, with a constant separate from Full Watch's. F+1 to F+4 set or remove the matching priority. F+EXIT adds or removes the channel from the Monitor list outside an active entry. The center of the screen shows "<< F WATCH >>" ("<<F WATCH>>" when four 4-digit channels require it) and the top bar shows FW.

Multilist Watch keeps its previous behavior, with its two priorities (without channel number bubbles). All F+ shortcuts work there as in VFO (F+0, F+5, F+6 to F+9, F+SK1/SK2, F+EXIT…), and the watch resumes once the main screen is idle. The radio registers, the squelch thresholds and the scan engine are kept.

After directly entering a channel number without a list, the arrows browse the channels in numerical order from that position, skipping empty slots.

History: the "HISTORY" action (F1/F2 short or long, M long) opens the history from the VFO, without starting a scan; the title moves to the status bar and the list gains a line. In Spectrum, Sentinel and from the VFO, a listened entry (arrows) uses its own modulation and bandwidth; long press on 1: the entry's frequency goes to the main VFO (frequency mode) and the screen returns to the VFO.

Messages: `*` switches between the HISTORY page and the MESSAGES page (the scan and the reception are stopped on MESSAGES). Each message received by FMessage is saved there automatically (external memory 0x012000, two 4 KB sectors, a free area in F4HWN); the 32 latest are listed as "01-AA: START", newest first, with an arrival number from 01 to 32 (it does not change after a deletion and restarts at 01 after CLEAR ALL) and a dotted line between two FMessage sessions. `M` opens the message, `6` deletes it (DELETE?), long `6` clears everything (CLEAR ALL?). The "MESSAGES" action (F1/F2, M long) opens the history directly on this page. Screen, reading and deletion are in the external "messages" module. The RX/TX log no longer exists.

FMessage: the "FMESSAGE" action (F1/F2, M long) opens the FMessage app (app shortcut 15); double beep if it is not installed.

REVERSE: "REVERSE:" ON/OFF line in the channel editor, "REVERSE FQ" action, "R" indicator in place of +/-; as in F4HWN, the setting is saved and also works in frequency mode.

Received audio aligned on F4HWN: chip default values in FM, AM and USB, SetRxG menu (RX noise gate, ON by default, forced ON with the RX compander; byte 0x00A0C0 as in F4HWN), compander fixed for the BK4829. TX powers and 1750 Hz tone level as in F4HWN; the 1750 Hz tone is heard in the speaker.

VFO saving: the frequency and the channel number are saved 3 s after the last change (less memory wear), and immediately before a transmission, the menu, a scan or an app.

Shortcuts: a held key (F1, F2, M long) triggers nothing else after its action; the history, Spectrum, Sentinel, scans and FMessage opened by a shortcut ignore the key still pressed until it is released.

VOX: taken from F4HWN (VOX menu OFF/1 to 10, "VOX" action, icon in the status bar; same setting bytes and calibration thresholds as F4HWN).

Scanner: Spectrum, the VFO scans (F+*, long press on *, ML Watch) and the history opened from Spectrum or the VFO trigger on a fixed rise of 5 (2.5 dB); their sensitivity is set by the dBm squelch. Sentinel and its history keep their dynamic DS threshold (keys * and F).

The FSystemInfo app shows the minimum free stack measured since start-up.

64 app slots. DTMF and Live Viewer not enabled.

## Build

With Docker:

```sh
bash build-with-docker.sh
```

Locally: ARM GNU Toolchain 13.2.Rel1 (`arm-none-eabi-gcc` in PATH), CMake >= 3.22, Ninja and Python 3 with `zopfli==0.4.3`.

```sh
python3 -m pip install zopfli==0.4.3
bash tools/build-local.sh
```

The all-in-one BINs (Falcon 1K and Falcon 4K) are generated in `firmware/`. The version is read from `sources/runtime/CMakePresets.json` (`VERSION_STRING_2`). `sources/runtime/build/S4K/onefile-measurements.json` (and `build/S1K` for the 1K) gives the use of the internal flash and of the external memory.

The code style is described by `sources/.clang-format`.

To rebuild the Falcon apps:

```sh
bash sources/fguide/guide/build.sh
bash sources/fsysteminfo/sysinfo/build.sh
bash sources/fmessage/fmessage/build.sh
bash sources/fmessage/fmsgoption/build.sh
```

## Tests

After building, install the dependencies, then run:

```sh
python3 -m pip install -r tools/requirements-tests.txt
bash tools/test-local.sh
```

The ARM tests run the built firmware with simulated peripherals; they do not replace a test on the radio. The FMessage and FMessOption tests are described in `sources/fmessage/README.md`.

## Integrity

`SHA256SUMS` holds the fingerprints of the files of the archive, except this file itself.

Falcon firmware is coded using Claude Code.
