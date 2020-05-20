#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#include "hem.h"
#include "input.h"

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

// How long to wait between simulated input events.
#define KEY_SEND_DELAY 64

typedef struct _HIEW_KEYS {
    PCHAR Key;
    PCHAR Description;
} HIEW_KEYS, *PHIEW_KEYS;

static HIEW_KEYS HiewKeys[] = {
    { "Ctrl+Alt", "information" },
    { "Ctrl+Backspace", "file history" },
    { "Ctrl+.", "start/stop recording macro to Macro0" },
    { "Ctrl+-", "Macro manager" },
    { "Ctrl+NumMult", "mark all" },
    { "Alt+NumMult", "resize block to current offset" },
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
                           NULL,
                           NULL);

    // Clean up.
    for (DWORD Key = 0; Key < _countof(HiewKeys); Key++) {
        HiewGate_FreeMemory(KeyList[Key]);
    }

    if (KeyNum-- <= 0) {
        HiewGate_Message("Error", "Action was cancelled.");
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
    INT Event;

    INPUT_RECORD InputRecord = {
        .EventType = KEY_EVENT,
    };

    // Wait for dialogs to clean up.
    Sleep(KEY_SEND_DELAY);

    InputKeys = lpvThreadParam;

    DecodeKeyString(InputKeys->Key, &InputRecord.Event.KeyEvent);

    WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE),
                      &InputRecord,
                      1,
                      &EventCount);

    return 0;
}
