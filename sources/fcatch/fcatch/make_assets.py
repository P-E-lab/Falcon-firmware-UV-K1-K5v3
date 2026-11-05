#!/usr/bin/env python3
"""FCatch assets, little-endian:
  0    config-proxy descriptor (28 B) naming FCatch itself: Falcon 3.1.1+ saves
       the block at STORE back to the app's own assets when it exits;
  28   the 104 DCS codes (u16), 236 the 50 CTCSS tones in 0.1 Hz (u16);
  336  the saved block (store_t, 116 B): magic, threshold 115 (-115 dBm),
       listen on, no history, no band filter."""
import struct, sys
DCS = [0o23,0o25,0o26,0o31,0o32,0o36,0o43,0o47,0o51,0o53,0o54,0o65,0o71,0o72,0o73,0o74,0o114,0o115,0o116,0o122,0o125,0o131,0o132,0o134,0o143,0o145,0o152,0o155,0o156,0o162,0o165,0o172,0o174,0o205,0o212,0o223,0o225,0o226,0o243,0o244,0o245,0o246,0o251,0o252,0o255,0o261,0o263,0o265,0o266,0o271,0o274,0o306,0o311,0o315,0o325,0o331,0o332,0o343,0o346,0o351,0o356,0o364,0o365,0o371,0o411,0o412,0o413,0o423,0o431,0o432,0o445,0o446,0o452,0o454,0o455,0o462,0o464,0o465,0o466,0o503,0o506,0o516,0o523,0o526,0o532,0o546,0o565,0o606,0o612,0o624,0o627,0o631,0o632,0o654,0o662,0o664,0o703,0o712,0o723,0o731,0o732,0o734,0o743,0o754]
CTCSS = [670,693,719,744,770,797,825,854,885,915,948,974,1000,1035,1072,1109,1148,1188,1230,1273,1318,1365,1413,1462,1514,1567,1598,1622,1655,1679,1713,1738,1773,1799,1835,1862,1899,1928,1966,1995,2035,2065,2107,2181,2257,2291,2336,2418,2503,2541]
assert len(DCS) == 104 and len(CTCSS) == 50
STORE, STORE_LEN, STORE_MAGIC = 336, 116, 0x48435446
proxy = struct.pack('<I16sHBBI', 0x31505843, b'FCatch'.ljust(16, b'\0'), STORE, STORE_LEN, 0, STORE_MAGIC)
tables = struct.pack('<104H', *DCS) + struct.pack('<50H', *CTCSS)
store = struct.pack('<IBBBBI', STORE_MAGIC, 115, 1, 0, 0, 0).ljust(STORE_LEN, b'\0')
assert len(proxy) == 28 and len(proxy + tables) == STORE
open(sys.argv[1], 'wb').write(proxy + tables + store)
