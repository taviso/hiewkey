#ifndef __INPUT_H
#define __INPUT_H

// Takes a PKEY_EVENT_RECORD, and translates it into a string.
BOOL EncodeKeyString(PKEY_EVENT_RECORD Record, PCHAR HotKey, SIZE_T MaxLen);

// Decodes a string of the form "Ctrl+Shift+A" into a PKEY_EVENT_RECORD
BOOL DecodeKeyString(LPCSTR HotKey, PKEY_EVENT_RECORD Record);

VOID PrintKeyEvent(PKEY_EVENT_RECORD Key);

#endif
