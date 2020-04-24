#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <ctype.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#include "winutil.h"
#include "input.h"

#pragma comment(lib, "USER32")

#define MAX_KEY_LEN 16

static INIT_ONCE KeyTablesInit = INIT_ONCE_STATIC_INIT;

static CHAR RegKeyNames[UCHAR_MAX][MAX_KEY_LEN];
static CHAR ExtKeyNames[UCHAR_MAX][MAX_KEY_LEN];

static BOOL CALLBACK InitializeKeyTables(PINIT_ONCE InitOnce,
                                         PVOID Parameter,
                                         PVOID *Context)
{
    for (DWORD Key = 0; Key < UCHAR_MAX; Key++) {
        GetKeyNameText((0 << 24) | (Key << 16), RegKeyNames[Key], MAX_KEY_LEN);
        GetKeyNameText((1 << 24) | (Key << 16), ExtKeyNames[Key], MAX_KEY_LEN);
    }
    return TRUE;
}

// I think you cannot possibly encode more than 9 keys into a key event, thats
// 8 modifiers and at most 1 non-modifier
// e.g.
// Caps Lock+Left Alt+Right Alt+Left Ctrl+Right Ctrl+Num Lock+Scroll Lock+Shift+X
#define MAX_KEY_COMBINATION 9

// Takes a PKEY_EVENT_RECORD, and translates it into a string.
BOOL EncodeKeyString(PKEY_EVENT_RECORD Record, PCHAR HotKey, SIZE_T MaxLen)
{
    WORD ScanCode = Record->wVirtualScanCode;
    WORD KeyCode = Record->wVirtualKeyCode;
    BOOL Enhanced = Record->dwControlKeyState & ENHANCED_KEY;
    DWORD NumKeys = 0;
    PCHAR KeyNames[MAX_KEY_COMBINATION] = {0};

    InitOnceExecuteOnce(&KeyTablesInit, InitializeKeyTables, NULL, NULL);

    ZeroMemory(HotKey, MaxLen);

    // These should be in order, for example it would be weird to say
    // "Alt+Ctrl+Delete", everyone says "Ctrl+Alt+delete". Therefore, Ctrl must
    // be checked first. I think the natural order is:
    // Ctrl, Alt,  Shift, {Num, Scroll, Caps}, Key
    //
    // It doesn't really matter what order you press the modifiers in.
    //
    // The exception is if you're *just* pressing modifiers (e.g. Ctrl+Alt)
    // Then the *last* key you pressed is printed last, because that's what the
    // scancode is and it makes a difference.

    if (Record->dwControlKeyState & LEFT_CTRL_PRESSED) {
        if (Record->dwControlKeyState & RIGHT_ALT_PRESSED) {
            // There is no difference between Ctrl+Right Alt and Right Alt
            // Don't print anything.
        } else if (KeyCode != VK_CONTROL && Enhanced) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC)];
        }
    }
    if (Record->dwControlKeyState & RIGHT_CTRL_PRESSED) {
        if (KeyCode != VK_CONTROL && !Enhanced) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_RCONTROL, MAPVK_VK_TO_VSC)];
        }
    }
    if (Record->dwControlKeyState & LEFT_ALT_PRESSED) {
        if (KeyCode != VK_MENU) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_LMENU, MAPVK_VK_TO_VSC)];
        } else if (Enhanced && (Record->dwControlKeyState & RIGHT_ALT_PRESSED)) {
            // This must be Alt+Right Alt
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_LMENU, MAPVK_VK_TO_VSC)];
        }
    }
    if (Record->dwControlKeyState & RIGHT_ALT_PRESSED) {
        if (KeyCode != VK_MENU) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_RMENU, MAPVK_VK_TO_VSC)];
        } else if (!Enhanced && (Record->dwControlKeyState & LEFT_ALT_PRESSED)) {
            // This must be Right Alt+Alt
            KeyNames[NumKeys++] = ExtKeyNames[MapVirtualKey(VK_RMENU, MAPVK_VK_TO_VSC)];
        }
    }

    // You can tell the difference between
    //  LShift and RShift in scancodes, but not as modifiers
    //
    // i.e. LShift+X, RShift+X, LShift+RShift+X are all the same.
    //
    if (Record->dwControlKeyState & SHIFT_PRESSED) {
        if (KeyCode != VK_SHIFT) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_SHIFT, MAPVK_VK_TO_VSC)];
        }
    }

    if (Record->dwControlKeyState & NUMLOCK_ON) {
        if (KeyCode != VK_NUMLOCK) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_NUMLOCK, MAPVK_VK_TO_VSC)];
        }
    }

    if (Record->dwControlKeyState & SCROLLLOCK_ON) {
        if (KeyCode != VK_SCROLL) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_SCROLL, MAPVK_VK_TO_VSC)];
        }
    }
    if (Record->dwControlKeyState & CAPSLOCK_ON) {
        if (KeyCode != VK_CAPITAL) {
            KeyNames[NumKeys++] = RegKeyNames[MapVirtualKey(VK_CAPITAL, MAPVK_VK_TO_VSC)];
        }
    }

    if (Record->dwControlKeyState & ENHANCED_KEY) {
        KeyNames[NumKeys++] = ExtKeyNames[Record->wVirtualScanCode];
    } else {
        KeyNames[NumKeys++] = RegKeyNames[Record->wVirtualScanCode];
    }

success:

    for (DWORD Key = 0; Key < NumKeys; Key++) {
        // A Key couldn't be decoded.
        if (KeyNames[Key] == NULL)
            return FALSE;

        strncat_s(HotKey, MaxLen, KeyNames[Key], _TRUNCATE);

        // Append a + if not already there.
        if (Key != NumKeys - 1) {
            strncat_s(HotKey, MaxLen, "+", _TRUNCATE);
        }
    }

    return NumKeys > 0;
}

// Decodes a string of the form "Ctrl+Shift+A" into a PKEY_EVENT_RECORD
BOOL DecodeKeyString(LPCSTR HotKey, PKEY_EVENT_RECORD Record)
{
    PCHAR KeyCombination = strdupa(HotKey);
    PCHAR CurKey;
    DWORD CtrlState = 0;

    ZeroMemory(Record, sizeof *Record);

    InitOnceExecuteOnce(&KeyTablesInit, InitializeKeyTables, NULL, NULL);

    for (CurKey = strtok(KeyCombination, "+");
         CurKey;
         CurKey = strtok(NULL, "+")) {
        DWORD NumElems = UCHAR_MAX;
        WORD ScanCode;
        UINT KeyCode;
        PCHAR Result;

        Result   = _lfind(CurKey, RegKeyNames, &NumElems, MAX_KEY_LEN, stricmp);
        ScanCode = (DWORD)(Result - (PCHAR) RegKeyNames) / MAX_KEY_LEN;

        if (!Result) {
            Result   = _lfind(CurKey, ExtKeyNames, &NumElems, MAX_KEY_LEN, stricmp);
            ScanCode = (DWORD)(Result - (PCHAR) ExtKeyNames) / MAX_KEY_LEN;

            CtrlState |= ENHANCED_KEY;
        }

        // Failed to decode keyname.
        if (!Result) {
            return FALSE;
        }

        KeyCode = MapVirtualKey(ScanCode, MAPVK_VSC_TO_VK);

        switch (KeyCode) {
            case VK_CAPITAL:
                CtrlState |= CAPSLOCK_ON;
                break;

            case VK_MENU:
            case VK_LMENU:
                if (CtrlState & ENHANCED_KEY) {
                    CtrlState |= RIGHT_ALT_PRESSED;
                    // It's not possible to set RIGHT_ALT_PRESSED without
                    // LEFT_CTRL_PRESSED
                    CtrlState |= LEFT_CTRL_PRESSED;
                } else {
                    CtrlState |= LEFT_ALT_PRESSED;
                }
                break;

            case VK_RMENU:
                CtrlState |= RIGHT_ALT_PRESSED;
                CtrlState |= LEFT_CTRL_PRESSED;
                break;

            case VK_CONTROL:
                if (CtrlState & ENHANCED_KEY) {
                    CtrlState |= RIGHT_CTRL_PRESSED;
                } else {
                    CtrlState |= LEFT_CTRL_PRESSED;
                }
                break;

            case VK_RCONTROL:
                CtrlState |= RIGHT_CTRL_PRESSED;
                break;

            case VK_NUMLOCK:
                CtrlState |= NUMLOCK_ON;
                break;

            case VK_SCROLL:
                CtrlState |= SCROLLLOCK_ON;
                break;

            case VK_SHIFT:
            case VK_LSHIFT:
            case VK_RSHIFT:
                CtrlState |= SHIFT_PRESSED;
                break;
        }

        // We've decoded the modifiers, but what if user just wants Ctrl+Alt?
        // Whatver the last key is, that is the scancode.
        Record->bKeyDown = TRUE;
        Record->wRepeatCount = 1;
        Record->wVirtualScanCode = ScanCode;
        Record->wVirtualKeyCode = KeyCode;
        Record->uChar.AsciiChar = MapVirtualKey(KeyCode, MAPVK_VK_TO_CHAR);
        Record->dwControlKeyState = CtrlState;

        // I dunno what the rules are for setting this field, I've tried
        // to guess them from observation.
        if (CtrlState & LEFT_CTRL_PRESSED) {
            if (CtrlState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
                Record->uChar.AsciiChar = 0;
            }
        }

        // In general, for ascii chars the lowercase form seems to be used.
        if (KeyCode == Record->uChar.AsciiChar && isalpha(KeyCode)) {
            Record->uChar.AsciiChar = tolower(KeyCode);
        }

        if (KeyCode == Record->uChar.AsciiChar && isdigit(KeyCode)) {
            if (CtrlState & SHIFT_PRESSED) {
                // I don't think there's any way to figure out what this should be
                // without localization, so I'll just simulate it.
                Record->uChar.AsciiChar = ")!@#$%^&*("[KeyCode - '0'];
            }
        }
    }

    return TRUE;
}

VOID DumpKeyCodes()
{
    InitOnceExecuteOnce(&KeyTablesInit, InitializeKeyTables, NULL, NULL);

    for (DWORD Key = 0; Key < UCHAR_MAX; Key++) {
        if (strlen(RegKeyNames[Key])) printf("R %02X %s\n", Key, RegKeyNames[Key]);
        if (strlen(ExtKeyNames[Key])) printf("E %02X %s\n", Key, ExtKeyNames[Key]);
    }
}

VOID PrintKeyEvent(PKEY_EVENT_RECORD Key)
{
    UINT MapScanCode = MapVirtualKey(Key->wVirtualKeyCode, MAPVK_VK_TO_VSC);
    UINT MapChar = MapVirtualKey(Key->wVirtualKeyCode, MAPVK_VK_TO_CHAR);
    UINT MapKeyCode = MapVirtualKey(Key->wVirtualScanCode, MAPVK_VSC_TO_VK);
    UINT MapExKeyCode = MapVirtualKey(Key->wVirtualScanCode, MAPVK_VSC_TO_VK_EX);

    printf("Key event:                     \n");
    printf(" bKeyDown:                   %u\n", Key->bKeyDown);
    printf(" wRepeatCount:               %u\n", Key->wRepeatCount);
    printf(" wVirtualKeyCode:         %#02x\n", Key->wVirtualKeyCode);
    printf("    MAPVK_VK_TO_VSC  =>   %#02x\n", MapScanCode);
    printf("    MAPVK_VK_TO_CHAR =>   %#02x\n", MapChar);
    printf(" wVirtualScanCode:        %#02x\n", Key->wVirtualScanCode);
    printf("    MAPVK_VSC_TO_VK =>    %#02x\n", MapKeyCode);
    printf("    MAPVK_VSC_TO_VK_EX => %#02x\n", MapExKeyCode);
    printf(" uChar:                        \n");
    printf("  UnicodeChar:     '%C' (%#04x)\n", Key->uChar.UnicodeChar, Key->uChar.UnicodeChar);
    printf("  AsciiChar:       '%c' (%#02x)\n", Key->uChar.AsciiChar, Key->uChar.AsciiChar);
    printf("    VkKeyScanA  =>        %#02x\n", VkKeyScanA(Key->uChar.AsciiChar));
    printf(" dwControlKeyState:          %u\n", Key->dwControlKeyState);

    if (Key->dwControlKeyState & CAPSLOCK_ON)
        printf("CAPSLOCK_ON\n");
    if (Key->dwControlKeyState & ENHANCED_KEY)
        printf("ENHANCED_KEY\n");
    if (Key->dwControlKeyState & LEFT_ALT_PRESSED)
        printf("LEFT_ALT_PRESSED\n");
    if (Key->dwControlKeyState & LEFT_CTRL_PRESSED)
        printf("LEFT_CTRL_PRESSED\n");
    if (Key->dwControlKeyState & NUMLOCK_ON)
        printf("NUMLOCK_ON\n");
    if (Key->dwControlKeyState & RIGHT_ALT_PRESSED)
        printf("RIGHT_ALT_PRESSED\n");
    if (Key->dwControlKeyState & RIGHT_CTRL_PRESSED)
        printf("RIGHT_CTRL_PRESSED\n");
    if (Key->dwControlKeyState & SCROLLLOCK_ON)
        printf("SCROLLLOCK_ON\n");
    if (Key->dwControlKeyState & SHIFT_PRESSED)
        printf("SHIFT_PRESSED\n");
}

