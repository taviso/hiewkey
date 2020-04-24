#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#include "hem.h"

static HEM_API Hem_EntryPoint(HEMCALL_TAG *);
static HEM_API Hem_Unload(void);

HEMINFO_TAG KeyboardHelper = {
    .cbSize         = sizeof(KeyboardHelper),
    .sizeOfInt      = sizeof(int),
    .sdkVerMajor    = HEM_SDK_VERSION_MAJOR,
    .sdkVerMinor    = HEM_SDK_VERSION_MINOR,
    .hemFlag        = HEM_FLAG_MODEMASK | HEM_FLAG_FILEMASK,
    .EntryPoint     = Hem_EntryPoint,
    .Unload         = Hem_Unload,
    .shortName      = "Keyboard Helper",
    .name           = "Hiew Keyboard Helper",
    .about1         = "This plugin can send key combinations that",
    .about2         = "dont work on alternative terminals.",
    .about3         = "",
};

// The maximum number of keys in a key combination.
#define MAX_KEYS 4

// How long to wait between simulated input events.
#define KEY_SEND_DELAY 64

typedef struct _HIEW_KEYS {
    PCHAR Key;
    PCHAR Description;
    DWORD Flags;
    KEY_EVENT_RECORD Sequence[MAX_KEYS];
} HIEW_KEYS, *PHIEW_KEYS;

#define KEY_FLAG_DEFAULT      (0)
#define KEY_FLAG_MAPCHAR      (1 << 0)
#define KEY_FLAG_MAPVSC       (1 << 1)
#define KEY_FLAG_MAPVSCEX     (1 << 2)
#define KEY_FLAG_NOUP         (1 << 3)
#define KEY_FLAG_MAPALL       (KEY_FLAG_MAPCHAR | KEY_FLAG_MAPVSC)

static HIEW_KEYS HiewKeys[] = {
    {
        "Ctrl-Alt", "information", KEY_FLAG_NOUP | KEY_FLAG_MAPVSC, {
            {
                .wVirtualKeyCode    = VK_CONTROL,
                .dwControlKeyState  = LEFT_CTRL_PRESSED,
            },
            {
                .wVirtualKeyCode    = VK_MENU,
                .dwControlKeyState  = LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED,
            },
        },
    },
    {
        "Ctrl-Backspace", "file history", KEY_FLAG_MAPVSC, {
            {
                .wVirtualKeyCode    = VK_CONTROL,
                .dwControlKeyState  = LEFT_CTRL_PRESSED,
            },
            {
                .wVirtualKeyCode    = VK_BACK,
                .uChar.AsciiChar    = 0x7f,
                .dwControlKeyState  = LEFT_CTRL_PRESSED
            },
        },
    },
    {
        "Ctrl-.", "start/stop recording macro to Macro0", KEY_FLAG_MAPVSC, {
            {
                .wVirtualKeyCode    = VK_CONTROL,
                .dwControlKeyState  = LEFT_CTRL_PRESSED,
            },
            {
                .wVirtualKeyCode    = VK_OEM_PERIOD,
                .uChar.AsciiChar    = 0,
                .dwControlKeyState  = LEFT_CTRL_PRESSED,
            },
        }
    }
};

DWORD WINAPI SendInputThread(LPVOID lpvThreadParam);

int HEM_EXPORT Hem_Load(HIEWINFO_TAG *HiewInfo)
{
    HiewGate_Set(HiewInfo);
    HiewInfo->hemInfo = &KeyboardHelper;
    return HEM_OK;
}

int HEM_API Hem_Unload()
{
    return HEM_OK;
}

static PCHAR HiewGate_StringDup(LPCSTR String)
{
    PCHAR Result;
    Result = HiewGate_GetMemory(strlen(String) + 1);
    return strcpy(Result, String);
}

HEM_BYTE *CallBackLine(int n, PVOID user)
{
    MessageBox(NULL, "linecallback", "ok", 0);
    return NULL;
}

int HEM_API Hem_EntryPoint(HEMCALL_TAG *HemCall)
{
    static HANDLE InputThread;
    PCHAR KeyList[_countof(HiewKeys)];
    DWORD KeyWidth = 0;
    DWORD KeyNum;

    if (HemCall->cbSize < sizeof(HEMCALL_TAG))
        return HEM_ERROR;

    // Generate the menu
    for (DWORD Key = 0; Key < _countof(HiewKeys); Key++) {
        CHAR MenuEntry[256];

        // Format entry..
        snprintf(MenuEntry,
                 sizeof MenuEntry, "%-16s - %s",
                 HiewKeys[Key].Key,
                 HiewKeys[Key].Description);

        KeyList[Key] = HiewGate_StringDup(MenuEntry);

        if (strlen(KeyList[Key]) > KeyWidth)
            KeyWidth = strlen(KeyList[Key]);
    }

    KeyNum = HiewGate_Menu("Choose Key",
                           KeyList,
                           _countof(HiewKeys),
                           KeyWidth,
                           0,
                           NULL,
                           NULL,
                           CallBackLine,
                           NULL);

    // Clean up.
    for (DWORD Key = 0; Key < _countof(HiewKeys); Key++) {
        HiewGate_FreeMemory(KeyList[Key]);
    }

    if (KeyNum-- <= 0) {
        HiewGate_Message("Error", "I think you quit");
        return HEM_OK;
    }

    InputThread = CreateThread(NULL, 0, SendInputThread, &HiewKeys[KeyNum], 0, 0);

    // I don't need to monitor the result
    CloseHandle(InputThread);

    return HEM_OK;
}

DWORD WINAPI SendInputThread(LPVOID lpvThreadParam)
{
    PHIEW_KEYS InputKeys;
    DWORD EventCount;
    WORD ScanCode;
    WORD ScanCodeEx;
    WORD KeyCode;
    CHAR AsciiChar;

    INPUT_RECORD InputRecord = {
        .EventType = KEY_EVENT,
    };

    // Wait for dialogs to clean up.
    Sleep(200);

    InputKeys = lpvThreadParam;

    for (DWORD Event = 0; Event < _countof(InputKeys->Sequence); Event++) {
        CopyMemory(&InputRecord.Event,
                   &InputKeys->Sequence[Event],
                   sizeof(KEY_EVENT_RECORD));

        // Figure out the missing fields.
        KeyCode     = InputRecord.Event.KeyEvent.wVirtualKeyCode;
        ScanCode    = MapVirtualKey(KeyCode, MAPVK_VK_TO_VSC);
        AsciiChar   = MapVirtualKey(KeyCode, MAPVK_VK_TO_CHAR);
        ScanCodeEx  = MapVirtualKey(KeyCode, MAPVK_VK_TO_VSC_EX);

        // If Keycode is zero, we've finished.
        if (KeyCode == 0)
            break;

        if (InputKeys->Flags & KEY_FLAG_MAPCHAR)
            InputRecord.Event.KeyEvent.uChar.AsciiChar = AsciiChar;

        if (InputKeys->Flags & KEY_FLAG_MAPVSC)
            InputRecord.Event.KeyEvent.wVirtualScanCode = ScanCode;

        if (InputKeys->Flags & KEY_FLAG_MAPVSCEX)
            InputRecord.Event.KeyEvent.wVirtualScanCode = ScanCodeEx;

        InputRecord.Event.KeyEvent.wRepeatCount = 1;
        InputRecord.Event.KeyEvent.bKeyDown     = TRUE;

        WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE),
                          &InputRecord,
                          1,
                          &EventCount);

        Sleep(KEY_SEND_DELAY);
    }

    // Don't undo the key presses
    if (InputKeys->Flags & KEY_FLAG_NOUP)
        return 0;

    for (DWORD Event = _countof(InputKeys->Sequence); Event > 0; Event--) {
        DWORD Event = 1;
        CopyMemory(&InputRecord.Event,
                   &InputKeys->Sequence[Event - 1],
                   sizeof(KEY_EVENT_RECORD));

        // Figure out the missing fields.
        KeyCode     = InputRecord.Event.KeyEvent.wVirtualKeyCode;
        ScanCode    = MapVirtualKey(KeyCode, MAPVK_VK_TO_VSC);
        AsciiChar   = MapVirtualKey(KeyCode, MAPVK_VK_TO_CHAR);
        ScanCodeEx  = MapVirtualKey(KeyCode, MAPVK_VK_TO_VSC_EX);

        // If Keycode is zero, this is empty.
        if (KeyCode == 0)
            continue;

        if (InputKeys->Flags & KEY_FLAG_MAPCHAR)
            InputRecord.Event.KeyEvent.uChar.AsciiChar = AsciiChar;

        if (InputKeys->Flags & KEY_FLAG_MAPVSC)
            InputRecord.Event.KeyEvent.wVirtualScanCode = ScanCode;

        if (InputKeys->Flags & KEY_FLAG_MAPVSCEX)
            InputRecord.Event.KeyEvent.wVirtualScanCode = ScanCodeEx;

        if (KeyCode == VK_CONTROL)
            InputRecord.Event.KeyEvent.dwControlKeyState &= ~LEFT_CTRL_PRESSED;

        if (KeyCode == VK_SHIFT)
            InputRecord.Event.KeyEvent.dwControlKeyState &= ~SHIFT_PRESSED;

        if (KeyCode == VK_MENU)
            InputRecord.Event.KeyEvent.dwControlKeyState &= ~LEFT_ALT_PRESSED;

        InputRecord.Event.KeyEvent.wRepeatCount = 1;
        InputRecord.Event.KeyEvent.bKeyDown     = FALSE;

        WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE),
                          &InputRecord,
                          1,
                          &EventCount);

        Sleep(KEY_SEND_DELAY);
    }

    return 0;
}
