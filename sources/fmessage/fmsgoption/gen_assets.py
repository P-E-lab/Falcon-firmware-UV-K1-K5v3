#!/usr/bin/env python3
# FMessage Option read-only assets: the configuration-proxy descriptor (it
# must start the assets), the texts and the 5x5 font of the Falcon lists.
#
#   ./gen_assets.py fmsgoption_assets.bin fmsgoption_assets.h
import os, struct, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from call_sounds import SOUNDS, USER_MAGIC

a = Assets("FMSGOPTION")

# app_config_proxy_t: "CXP1", target app name, offset and length of its
# user block, reserved byte, the block's marker (../fmessage_user.h).
a.prefix("PROXY", struct.pack("<I16sHBBI", 0x31505843, b"FMessage", 0, 8, 0, USER_MAGIC))

groups = [
    ("TITLE", ["FMESSAGE OPTION", "KEYS INFO", "CALLSIGN"]),
    ("ROW", ["CALLSIGN:", "RX SOUND:", "CALL SOUND:", "KEYS INFO"]),
    ("VAL", ["OFF", "ON"]),
    ("SOUND", ["%02u %s" % (i + 1, n) for i, n in enumerate(SOUNDS)]),  # "01 STWARS"
    # three per view (left, center, right): list, keys info, callsign
    ("HINT", ["M:CHANGE", "F:PLAY", "EXIT:SAVE", "< >:SCROLL", "", "EXIT:BACK",
              "0-9:TYPE  M:OK", "", "EXIT:CANCEL"]),
    ("NOAPP", ["INSTALL FMESSAGE", "FIRST"]),
    ("KEYSET", [" ", ",.?!'-", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6",
                "PQRS7", "TUV8", "WXYZ9"]),
    ("KEYS", ["HOME", "PTT: CALL", "M: INBOX", "F: SEND / RESEND",
              "0-9: LETTERS", "HOLD 0-9: DIGIT", "*: DELETE",
              "HOLD *: CLEAR ALL", "< >: MOVE CURSOR", "EXIT: QUIT",
              "INBOX", "M: OPEN   EXIT: BACK", "*: DELETE",
              "MESSAGE", "M: RESPOND", "*: DELETE", "EXIT: BACK"]),
]
strings = []
for name, items in groups:
    a.const("X_" + name, len(strings))
    a.const("N_" + name, len(items))
    strings += items
a.table("STR", strings)

# 5x5 font, ASCII 0x20..0x5F (rows, bit 4 = left column): the Falcon list glyphs.
F = {c: [0] * 5 for c in range(0x20, 0x60)}
letters = [[14,17,31,17,17],[30,17,30,17,30],[15,16,16,16,15],[30,17,17,17,30],
           [31,16,30,16,31],[31,16,30,16,16],[15,16,19,17,15],[17,17,31,17,17],
           [14,4,4,4,14],[7,2,2,18,12],[17,18,28,18,17],[16,16,16,16,31],
           [17,27,21,17,17],[17,25,21,19,17],[14,17,17,17,14],[30,17,30,16,16],
           [14,17,17,21,10],[30,17,30,18,17],[15,16,14,1,30],[31,4,4,4,4],
           [17,17,17,17,14],[17,17,17,10,4],[17,17,21,21,10],[17,10,4,10,17],
           [17,10,4,4,4],[31,2,4,8,31]]
digits = [[14,17,17,17,14],[4,12,4,4,14],[30,1,14,16,31],[30,1,14,1,30],
          [18,18,31,2,2],[31,16,30,1,30],[14,16,30,17,14],[31,1,2,4,4],
          [14,17,14,17,14],[14,17,15,1,14]]
for i, g in enumerate(letters): F[0x41 + i] = g
for i, g in enumerate(digits):  F[0x30 + i] = g
F[ord('!')] = [4,4,4,0,4];   F[ord('*')] = [0,21,14,21,0]
F[ord("'")] = [4,4,0,0,0]
F[ord(',')] = [0,0,0,4,8];   F[ord('-')] = [0,0,14,0,0]
F[ord('.')] = [0,0,0,0,4];   F[ord('/')] = [1,2,4,8,16]
F[ord(':')] = [0,4,0,4,0];   F[ord('<')] = [2,4,8,4,2]
F[ord('>')] = [8,4,2,4,8];   F[ord('?')] = [14,17,2,0,4]
a.u8("FONT", [b for c in range(0x20, 0x60) for b in F[c]])
# Call melodies for F:PLAY, the same catalogue as FMessage: (Hz, ms) pairs.
index, words = [], []
for m in SOUNDS.values():
    assert len(words) // 2 <= 255 and len(m) // 2 <= 255
    index += [len(words) // 2, len(m) // 2]
    words += m
a.u16("MELODY", words)
a.u8("MELODY_INDEX", index)
a.const("MELODY_MAX", max(len(m) for m in SOUNDS.values()))
a.main()
