#ifndef FMESSAGE_USER_H
#define FMESSAGE_USER_H

/* User block at asset offset 0 of FMessage: its settings.  FMessage only
 * reads it; FMessage Option writes it through the configuration proxy, after
 * it closes.  Installing or updating FMessage restores the defaults.  Bump the
 * marker when the layout changes: the proxy then finds no target and
 * FMessage Option refuses to save. */
#define FMU_MAGIC     0x34534D46u /* "FMS4": numbered call sounds, STWARS first */
#define FMU_CALLSIGN  4u          /* 2 characters, no NUL */
#define FMU_RX_SOUND  6u          /* 0 = off, 1 = on */
#define FMU_CALL      7u          /* call sound: index in the melody list */
#define FMU_LEN       8u
#define FMU_SOUNDS    17u         /* shared catalogue: call_sounds.py */

#define FMESSAGE_APP_NAME "FMessage"

#endif
