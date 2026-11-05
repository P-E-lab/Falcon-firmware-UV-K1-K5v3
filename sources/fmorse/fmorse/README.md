# FMorse 1.7

Formerly Morse (Light/Radio) 1.6; renamed, behaviour unchanged.


Light or radio-tone Morse sender, ABI 1 / API 2. Predefined messages: OK, SOS,
STOP and NO; custom messages contain up to 32 characters using numeric multitap.
Choose message, speed, one-shot/repeat and output in the app. `#` opens key help.
Reception on the loader-selected VFO and its green receive LED remain available
when the app is not transmitting. Radio output waits 1000 ms on a muted carrier
after TX/tone preparation, before the first Morse element of each message.
MENU/EXIT and TX checks remain active during that wait. Light output and Morse
element timing are unchanged. No resident firmware or module update is needed.

From this directory, run `bash build.sh` with ARM GNU tools and Python 3 in PATH.
The app uses only the shared API and does not call fixed firmware addresses.
