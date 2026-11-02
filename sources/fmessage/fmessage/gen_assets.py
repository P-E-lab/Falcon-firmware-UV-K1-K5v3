#!/usr/bin/env python3
# FMessage read-only assets: the user block (settings, written by FMessage
# Option), the texts, the call melodies and the BK4819 FSK register sequences.
#
#   ./gen_assets.py fmessage_assets.bin fmessage_assets.h
import os, struct, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets
from call_sounds import SOUNDS, USER_MAGIC, DEFAULT_SOUND

a = Assets("FMESSAGE")

# ---- user block at offset 0 (see ../fmessage_user.h): defaults ----
# magic FMS4, callsign AA, RX sound on, call sound 01 STWARS
a.prefix("USER", struct.pack("<I", USER_MAGIC) + b"AA" + bytes([1, list(SOUNDS).index(DEFAULT_SOUND)]))

# ---- texts: one table, entry i at STR + i * STR_STRIDE ----
groups = [
    ("WORD", [" - READY", " - BUSY", "HERE:", "MSG:", "RESEND", " NEW", " - CALL", "NO MESSAGE"]),
    # status-line title per screen (home: the callsign line; message: + sender)
    ("TITLE", ["", "INBOX", "FROM ", "QUIT?", "DELETE?"]),
    ("QUIT", ["MESSAGES WILL", "BE LOST", "THIS MESSAGE", "WILL BE LOST"]),
    ("KEYSET", [" ", ",.?!'-", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6",
                "PQRS7", "TUV8", "WXYZ9"]),
    # key hints, small font at the bottom, one per screen
    # three per screen: left, center, right
    ("HINT", ["", "", "", "M:OPEN", "*:DEL", "EXIT:BACK",
              "M:RESPOND", "*:DEL", "EXIT:BACK", "M:YES", "", "EXIT:NO", "M:YES", "", "EXIT:NO"]),
]
strings = []
for name, items in groups:
    a.const("X_" + name, len(strings))
    strings += items
a.table("STR", strings)

# ---- call melodies (the Falcon Roger beeps): (Hz, ms) pairs, 0 Hz = gap ----
MELODIES = list(SOUNDS.values())
index, words = [], []
for m in MELODIES:
    assert len(words) // 2 <= 255 and len(m) // 2 <= 255
    index += [len(words) // 2, len(m) // 2]
    words += m
a.u16("MELODY", words)
a.u8("MELODY_INDEX", index)  # offsets in pairs, not words: compact 8-bit index
a.const("MELODY_MAX", max(len(m) for m in MELODIES))

# ---- BK4819 FSK register sequences: (register, value), SEQ_DELAY = wait ms ----
# The Beam app routines (BK4819_SetupAircopy / ResetFSK / PrepareFSKReceive /
# SendFSKData) with a 56-byte frame.
SEQ_DELAY = 0xFF
a.const("SEQ_DELAY", SEQ_DELAY)

def seq(name, pairs):
    a.u16(name, [w for pair in pairs for w in pair])

RESET = [(0x3F, 0x0000), (0x59, 0x0068), (SEQ_DELAY, 30), (0x30, 0x0000)]
# On exit, also turn Tone2 and FSK off (as F4HWN after FSK): the resident
# RADIO_SetupRegisters leaves REG_58 alone, and FSK left on silences the mic.
seq("SEQ_RESET", RESET + [(0x70, 0x0000), (0x58, 0x0000)])
seq("SEQ_SETUP", [
    (0x70, 0x00C3),             # Tone2 on, tuning gain 48
    (0x72, 0x3065),             # Tone2 baudrate 1200
    (0x58, 0x00C1),             # FSK enable, 1.2K
    (0x5C, 0x5665),             # hardware CRC on
    (0x5D, 0x4700),             # FSK data length 72 bytes
    (0x5E, 0x3204),
] + RESET)
seq("SEQ_RX", RESET + [
    (0x02, 0x0000), (0x3F, 0x0000),
    (0x37, 0x9F1F), (0x30, 0x0000), (0x30, 0xBFF1),   # RX on
    (0x3F, 0x3000),             # FSK RX finished | FIFO almost full
    (0x59, 0x4068), (0x59, 0x3068),                   # clear FIFO, RX enable
])
seq("SEQ_TX_LOAD", [(SEQ_DELAY, 30), (0x3F, 0x8000), (0x59, 0x8068), (0x59, 0x0068)])
seq("SEQ_TX_GO", [(SEQ_DELAY, 20), (0x59, 0x2868)])
seq("SEQ_TX_END", [(0x02, 0x0000), (SEQ_DELAY, 30)] + RESET + [(0x36, 0x0088)])
a.main()
