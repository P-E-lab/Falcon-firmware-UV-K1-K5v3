# FSnake 1.6

Formerly Snake F 1.5; renamed, game unchanged.

Rebuilt from the exact official Snake 1.1 source matching the uploaded Snake.app. Apache-2.0, original credits retained.

Only requested changes: 3 selects UP while moving horizontally and RIGHT while moving vertically; 7 selects DOWN while moving horizontally and LEFT while moving vertically. 2/8/4/6 keep their original direction controls. Edges wrap to the same row or column on the opposite edge, preserving heading. Self-collision rules, graphics, score, pause, restart, saver, original aliases and 120ms countdown / 20ms game loop are preserved.

The original 20ms delay is split into twenty 1ms slices. Keys are also sampled
after drawing and after the LCD transfer. Additional samples do not consume the
repeat timer: only elapsed delay slices advance it. No new movement clock,
diagonals or direction queue is introduced; movement interval/countdown values
are retained. Held 3/7 does not cause repeated alternating turns. Short 1ms
presses, held contextual keys, forbidden reversals, loader entry/exit and nominal
frame pacing are checked with the actual ARM app under mocked IO. Physical
keyboard scanning and display transfer still contribute to response time.

Compatible with F4HWN Labs 6.2.0 and Falcon overlay firmware, ABI 1 / API 2.
The package identity is Snake F, distinct from official Snake. UVStudio no longer
mistakes this custom version for a newer incompatible official release. Game rules
and assets are unchanged; only input sampling is revised. The loader's name-based configuration owner
means the old Snake high score is not automatically imported. No new firmware or
module is needed. Install this local package through UVStudio Apps.

Hardware is mocked; no physical-radio test is claimed.

From this directory, build with ARM_PREFIX set to an arm-none-eabi- compiler prefix, then bash build.sh. Code/global image: 1980 bytes; unchanged original assets: 1178 bytes; .app: 3222 bytes.
