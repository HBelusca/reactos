/*
 * PROJECT:     ReactOS i8042 (ps/2 keyboard-mouse controller) driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/input/i8042prt/i8042prt.c
 * PURPOSE:     Reading the registry
 * PROGRAMMERS: Copyright Victor Kirhenshtein (sauros@iname.com)
                Copyright Jason Filby (jasonfilby@yahoo.com)
                Copyright Martijn Vernooij (o112w8r02@sneakemail.com)
                Copyright 2006-2007 Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "i8042prt.h"

#include <debug.h>

/* FUNCTIONS *****************************************************************/

/* See https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/forcing-a-system-crash-from-the-keyboard#dump1keys */
static
const UCHAR keyToScanTbl[134] = {
    0x00,0x29,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,
    0x0A,0x0B,0x0C,0x0D,0x7D,0x0E,0x0F,0x10,0x11,0x12,
    0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x00,
    0x3A,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,
    0x27,0x28,0x2B,0x1C,0x2A,0x00,0x2C,0x2D,0x2E,0x2F,
    0x30,0x31,0x32,0x33,0x34,0x35,0x73,0x36,0x1D,0x00,
    0x38,0x39,0xB8,0x00,0x9D,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xD2,0xD3,0x00,0x00,0xCB,
    0xC7,0xCF,0x00,0xC8,0xD0,0xC9,0xD1,0x00,0x00,0xCD,
    0x45,0x47,0x4B,0x4F,0x00,0xB5,0x48,0x4C,0x50,0x52,
    0x37,0x49,0x4D,0x51,0x53,0x4A,0x4E,0x00,0x9C,0x00,
    0x01,0x00,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,
    0x43,0x44,0x57,0x58,0x00,0x46,0x00,0x00,0x00,0x00,
    0x00,0x7B,0x79,0x70 };

static inline
USHORT
KeyNumToScanCode(
    _In_ ULONG KeyNumber)
{
    DBGKEY_SCANCODE Scan;

    /* Index 124 (SysReq) is a special case because
     * an 84-key keyboard has a different scan code */
    if (KeyNumber == 124)
    {
        Scan.Code  = KEYBOARD_DEBUG_HOTKEY_ENH | 0x80;
        Scan.Code2 = KEYBOARD_DEBUG_HOTKEY_AT;
    }
    else
    {
        if (KeyNumber < _countof(keyToScanTbl))
            Scan.Code = keyToScanTbl[KeyNumber];
        else
            Scan.Code = 0;
        Scan.Code2 = 0;
    }
    return Scan.AsShort;
}

/**
 * @brief
 * Retrieves the Manual Bugcheck/CrashDump key settings from the registry.
 * They are stored in DeviceExtension->Dump1Keys & DeviceExtension->Dump2Key.
 *
 * @note
 * Based on WDK pnpi8042 sample driver I8xServiceCrashDump() function.
 **/
VOID
i8042ServiceCrashDump(
    _In_ PUNICODE_STRING RegistryPath,
    _Inout_ PI8042_SETTINGS Settings)
{
    NTSTATUS Status;
    ULONG defaultCrashFlags = 0;
    ULONG crashFlags; // Dump1Keys
    ULONG defaultKeyNumber = 0;
    ULONG keyNumber;  // Dump2Key
    RTL_QUERY_REGISTRY_TABLE Parameters[4];

    PAGED_CODE();

    RtlZeroMemory(Parameters, sizeof(Parameters));

    Parameters[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Parameters[0].Name = L"Crashdump";

    Parameters[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[1].Name = L"Dump1Keys";
    Parameters[1].EntryContext = &crashFlags;
    Parameters[1].DefaultType = REG_DWORD;
    Parameters[1].DefaultData = &defaultCrashFlags;
    Parameters[1].DefaultLength = sizeof(ULONG);

    Parameters[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[2].Name = L"Dump2Key";
    Parameters[2].EntryContext = &keyNumber;
    Parameters[2].DefaultType = REG_DWORD;
    Parameters[2].DefaultData = &defaultKeyNumber;
    Parameters[2].DefaultLength = sizeof(ULONG);

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
                                    RegistryPath->Buffer,
                                    Parameters,
                                    NULL,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        /* Go ahead and assign driver defaults */
        Settings->CrashFlags = defaultCrashFlags;
    }
    else
    {
        Settings->CrashFlags = crashFlags;
    }

    if (Settings->CrashFlags)
        Settings->CrashScan.AsShort = KeyNumToScanCode(keyNumber);
    // Settings->CrashScanCode;
    // Settings->CrashScanCode2;

    INFO_(I8042PRT, "ServiceCrashDump: CrashFlags = 0x%x\n",
          Settings->CrashFlags);
    INFO_(I8042PRT, "ServiceCrashDump: CrashScanCode = 0x%02x, CrashScanCode2 = 0x%02x\n",
          Settings->CrashScan.Code, Settings->CrashScan.Code2);
}

/**
 * @brief
 * Retrieves the Debugger-Enable key settings from the registry.
 * They are stored in DeviceExtension->DebugEnableScan.Code & DeviceExtension->DebugEnableScan.Code2.
 *
 * @note
 * Based on WDK pnpi8042 sample driver I8xServiceDebugEnable() function.
 **/
VOID
i8042ServiceDebugEnable(
    _In_ PUNICODE_STRING RegistryPath,
    _Inout_ PI8042_SETTINGS Settings)
{
    NTSTATUS Status;
    ULONG defaultDebugFlags = 0;
    ULONG debugFlags; // Debug1Keys
    ULONG defaultKeyNumber = 0;
    ULONG keyNumber;  // Debug2Key
    RTL_QUERY_REGISTRY_TABLE Parameters[4];

    PAGED_CODE();

    RtlZeroMemory(Parameters, sizeof(Parameters));

    Parameters[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Parameters[0].Name = L"DebugEnable";

    Parameters[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[1].Name = L"Debug1Keys";
    Parameters[1].EntryContext = &debugFlags;
    Parameters[1].DefaultType = REG_DWORD;
    Parameters[1].DefaultData = &defaultDebugFlags;
    Parameters[1].DefaultLength = sizeof(ULONG);

    Parameters[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[2].Name = L"Debug2Key";
    Parameters[2].EntryContext = &keyNumber;
    Parameters[2].DefaultType = REG_DWORD;
    Parameters[2].DefaultData = &defaultKeyNumber;
    Parameters[2].DefaultLength = sizeof(ULONG);

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
                                    RegistryPath->Buffer,
                                    Parameters,
                                    NULL,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        /* Go ahead and assign driver defaults */
        Settings->DebugEnableFlags = defaultDebugFlags;
    }
    else
    {
        Settings->DebugEnableFlags = debugFlags;
    }

    if (Settings->DebugEnableFlags)
        Settings->DebugEnableScan.AsShort = KeyNumToScanCode(keyNumber);
    // Settings->DebugEnableScanCode;
    // Settings->DebugEnableScanCode2;

    INFO_(I8042PRT, "ServiceDebugEnable: DebugFlags = 0x%x\n",
          Settings->DebugEnableFlags);
    INFO_(I8042PRT, "ServiceDebugEnable: DebugScanCode = 0x%02x, DebugScanCode2 = 0x%02x\n",
          Settings->DebugEnableScan.Code, Settings->DebugEnableScan.Code2);
}

VOID
i8042ServiceDebugSupport(
    _In_ PUNICODE_STRING RegistryPath,
    _Inout_ PI8042_SETTINGS Settings)
{
    NTSTATUS Status = STATUS_SUCCESS;
    RTL_QUERY_REGISTRY_TABLE Parameters[5];

    ULONG DefaultBreakOnSysRq = 1;

    /* Default values depending on whether we are
     * running a debug build or a normal build. */
#if DBG
    ULONG DefaultKdEnableOnCtrlSysRq = 1;
    ULONG DefaultCrashOnCtrlScroll = 1;
#else // In principle we should just default to what's done below even on debug builds.
    ULONG DefaultKdEnableOnCtrlSysRq = 0;
    ULONG DefaultCrashOnCtrlScroll = 0;
#endif
    ULONG KdEnableOnCtrlSysRq;
    ULONG CrashOnCtrlScroll;

    PAGED_CODE();

    RtlZeroMemory(Parameters, sizeof(Parameters));

    Parameters[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Parameters[0].Name = L"Parameters";

    Parameters[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[1].Name = L"BreakOnSysRq";
    Parameters[1].EntryContext = &Settings->BreakOnSysRq;
    Parameters[1].DefaultType = REG_DWORD;
    Parameters[1].DefaultData = &DefaultBreakOnSysRq;
    Parameters[1].DefaultLength = sizeof(ULONG);

    Parameters[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[2].Name = L"KdEnableOnCtrlSysRq";
    Parameters[2].EntryContext = &KdEnableOnCtrlSysRq;
    Parameters[2].DefaultType = REG_DWORD;
    Parameters[2].DefaultData = &DefaultKdEnableOnCtrlSysRq;
    Parameters[2].DefaultLength = sizeof(ULONG);

    Parameters[3].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[3].Name = L"CrashOnCtrlScroll";
    Parameters[3].EntryContext = &CrashOnCtrlScroll;
    Parameters[3].DefaultType = REG_DWORD;
    Parameters[3].DefaultData = &DefaultCrashOnCtrlScroll;
    Parameters[3].DefaultLength = sizeof(ULONG);

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
                                    RegistryPath->Buffer,
                                    Parameters,
                                    NULL,
                                    NULL);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND) // (!NT_SUCCESS(Status))
    {
        /* Registry path doesn't exist. Set defaults */
        Settings->BreakOnSysRq = DefaultBreakOnSysRq;
        KdEnableOnCtrlSysRq = DefaultKdEnableOnCtrlSysRq;
        CrashOnCtrlScroll = DefaultCrashOnCtrlScroll;
    }


    if (Settings->BreakOnSysRq)
        INFO_(I8042PRT, "Breaking in KD Debugger on SysRq\n");

    if (KdEnableOnCtrlSysRq)
    {
        INFO_(I8042PRT, "Enabling KD Debugger on RCtrl + SysRq\n");
        Settings->DebugEnableFlags = CRASH_R_CTRL;
        Settings->DebugEnableScan.Code = KEYBOARD_DEBUG_HOTKEY_ENH | 0x80;
        Settings->DebugEnableScan.Code2 = KEYBOARD_DEBUG_HOTKEY_AT;
    }
    else
    {
        i8042ServiceDebugEnable(RegistryPath, Settings);
        if (Settings->DebugEnableFlags || Settings->DebugEnableScan.Code || Settings->DebugEnableScan.Code2)
            INFO_(I8042PRT, "Enabling KD Debugger on custom user keys\n");
    }

    if (CrashOnCtrlScroll)
    {
        INFO_(I8042PRT, "Crashing on RCtrl + Scroll Lock\n");
        Settings->CrashFlags = CRASH_R_CTRL;
        Settings->CrashScan.Code = SCROLL_LOCK_SCANCODE;
        Settings->CrashScan.Code2 = 0;
    }
    else
    {
        i8042ServiceCrashDump(RegistryPath, Settings);
        if (Settings->CrashFlags || Settings->CrashScan.Code || Settings->CrashScan.Code2)
            INFO_(I8042PRT, "Crashing on custom user keys\n");
    }
}

/**
 * @brief
 * Monitors whether the debugging support keys are pressed,
 * and deals with them accordingly.
 *
 * @note
 * Based on WDK pnpi8042 sample driver I8xProcessCrashDump() function.
 **/
VOID
i8042ProcessCrashDump(
    _In_ PPORT_DEVICE_EXTENSION DeviceExtension, // PPORT_KEYBOARD_EXTENSION
    _In_ UCHAR ScanCode,
    _In_ KEYBOARD_SCAN_STATE ScanState)
{
    ULONG crashFlags = DeviceExtension->Settings.CrashFlags;
    ULONG debugFlags = DeviceExtension->Settings.DebugEnableFlags;
    UCHAR crashScanCode  = DeviceExtension->Settings.CrashScan.Code;
    UCHAR crashScanCode2 = DeviceExtension->Settings.CrashScan.Code2;
    UCHAR debugScanCode  = DeviceExtension->Settings.DebugEnableScan.Code;
    UCHAR debugScanCode2 = DeviceExtension->Settings.DebugEnableScan.Code2;

#define IS_VALID_ACTION_CODE(Code, ScanCode, ScanState) \
    ( (IS_MAKE_CODE(Code) && (ScanState) == Normal && GET_MAKE_CODE(ScanCode) == (Code)) || \
     (IS_BREAK_CODE(Code) && (ScanState) == GotE0  && GET_MAKE_CODE(ScanCode) == GET_MAKE_CODE(Code)) )

#if 0
    if (crashFlags == 0 && debugFlags == 0)
        return;
#endif

    if (IS_MAKE_CODE(ScanCode))
    {
        /*
         * Make code
         *
         * If it is one of the crash flag keys record it.
         * If it is a crash dump key record it.
         * If it is neither, reset the current tracking state.
         */
// NOTE: CurrentCrashFlags == DumpFlags
        switch (ScanCode)
        {
        case CTRL_SCANCODE:
            if (ScanState == Normal)     // Left
                DeviceExtension->CurrentCrashFlags |= CRASH_L_CTRL;
            else if (ScanState == GotE0) // Right
                DeviceExtension->CurrentCrashFlags |= CRASH_R_CTRL;
            break;

        case ALT_SCANCODE:
            if (ScanState == Normal)     // Left
                DeviceExtension->CurrentCrashFlags |= CRASH_L_ALT;
            else if (ScanState == GotE0) // Right
                DeviceExtension->CurrentCrashFlags |= CRASH_R_ALT;
            break;

        case LEFT_SHIFT_SCANCODE:
            if (ScanState == Normal)
                DeviceExtension->CurrentCrashFlags |= CRASH_L_SHIFT;
            break;

        case RIGHT_SHIFT_SCANCODE:
            if (ScanState == Normal)
                DeviceExtension->CurrentCrashFlags |= CRASH_R_SHIFT;
            break;

        default:
            if (IS_VALID_ACTION_CODE(crashScanCode , ScanCode, ScanState) ||
                IS_VALID_ACTION_CODE(crashScanCode2, ScanCode, ScanState) ||
                IS_VALID_ACTION_CODE(debugScanCode , ScanCode, ScanState) ||
                IS_VALID_ACTION_CODE(debugScanCode2, ScanCode, ScanState))
            {
                /* A key we are looking for */
                break;
            }
            /* Not a key we are interested in, reset our current state */
            DeviceExtension->CurrentCrashFlags = 0;
            break;
        }
    }
    else
    {
        /*
         * Break code
         *
         * If one of the modifier keys is released, our state is reset and
         *  all keys have to be pressed again.
         * If it is a non modifier key, proceed with the processing if it is
         *  the crash dump key, otherwise reset our tracking state.
         */
        switch (GET_MAKE_CODE(ScanCode))
        {
        case CTRL_SCANCODE:
            if (ScanState == Normal)     // Left
            {
                DeviceExtension->CurrentCrashFlags &=
                    ~(CRASH_BOTH_TIMES | DEBUG_ENABLE_BOTH_TIMES | CRASH_L_CTRL);
            }
            else if (ScanState == GotE0) // Right
            {
                DeviceExtension->CurrentCrashFlags &=
                    ~(CRASH_BOTH_TIMES | DEBUG_ENABLE_BOTH_TIMES | CRASH_R_CTRL);
            }
            break;

        case ALT_SCANCODE:
            if (ScanState == Normal)     // Left
            {
                DeviceExtension->CurrentCrashFlags &=
                    ~(CRASH_BOTH_TIMES | DEBUG_ENABLE_BOTH_TIMES | CRASH_L_ALT);
            }
            else if (ScanState == GotE0) // Right
            {
                DeviceExtension->CurrentCrashFlags &=
                    ~(CRASH_BOTH_TIMES | DEBUG_ENABLE_BOTH_TIMES | CRASH_R_ALT);
            }
            break;

        case LEFT_SHIFT_SCANCODE:
            if (ScanState == Normal)
            {
                DeviceExtension->CurrentCrashFlags &=
                    ~(CRASH_BOTH_TIMES | DEBUG_ENABLE_BOTH_TIMES | CRASH_L_SHIFT);
            }
            break;

        case RIGHT_SHIFT_SCANCODE:
            if (ScanState == Normal)
            {
                DeviceExtension->CurrentCrashFlags &=
                    ~(CRASH_BOTH_TIMES | DEBUG_ENABLE_BOTH_TIMES | CRASH_R_SHIFT);
            }
            break;

        default:
            if (IS_VALID_ACTION_CODE(crashScanCode , ScanCode, ScanState) ||
                IS_VALID_ACTION_CODE(crashScanCode2, ScanCode, ScanState))
            {
                if (DeviceExtension->CurrentCrashFlags & CRASH_FIRST_TIME)
                    DeviceExtension->CurrentCrashFlags |= CRASH_SECOND_TIME;
                else
                    DeviceExtension->CurrentCrashFlags |= CRASH_FIRST_TIME;

                crashFlags |= CRASH_BOTH_TIMES;

                if (DeviceExtension->CurrentCrashFlags == crashFlags)
                {
                    DeviceExtension->CurrentCrashFlags = 0;

                    /* Bring down the system in a somewhat controlled manner */
                    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0, 0, 0, 0);
                }
            }
            else if (IS_VALID_ACTION_CODE(debugScanCode , ScanCode, ScanState) ||
                     IS_VALID_ACTION_CODE(debugScanCode2, ScanCode, ScanState))
            {
                if (DeviceExtension->CurrentCrashFlags & DEBUG_ENABLE_FIRST_TIME)
                    DeviceExtension->CurrentCrashFlags |= DEBUG_ENABLE_SECOND_TIME;
                else
                    DeviceExtension->CurrentCrashFlags |= DEBUG_ENABLE_FIRST_TIME;

                debugFlags |= DEBUG_ENABLE_BOTH_TIMES;

                if (DeviceExtension->CurrentCrashFlags == debugFlags)
                {
                    BOOLEAN Enable = FALSE;
                    DeviceExtension->CurrentCrashFlags = 0;

                    /* Enable the debugger */
                    KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE, sizeof(Enable), &Enable, 0, NULL, NULL);
                    KdEnableDebugger();
                }
            }
            else
            {
                /* Not a key we are looking for, reset state */
                DeviceExtension->CurrentCrashFlags = 0;
            }

            break;
        }
    }

#undef IS_VALID_ACTION_CODE
}


NTSTATUS
ReadRegistryEntries(
    IN PUNICODE_STRING RegistryPath,
    OUT PI8042_SETTINGS Settings)
{
    NTSTATUS Status;
    ULONG i;
    RTL_QUERY_REGISTRY_TABLE Parameters[16];

    ULONG DefaultKeyboardDataQueueSize = 0x64;
    PCWSTR DefaultKeyboardDeviceBaseName = L"KeyboardPort";
    ULONG DefaultMouseDataQueueSize = 0x64;
    ULONG DefaultMouseResolution = 3;
    ULONG DefaultMouseSynchIn100ns = 20000000;
    ULONG DefaultNumberOfButtons = 2;
    PCWSTR DefaultPointerDeviceBaseName = L"PointerPort";
    ULONG DefaultPollStatusIterations = 1;
    ULONG DefaultOverrideKeyboardType = 4;
    ULONG DefaultOverrideKeyboardSubtype = 0;
    ULONG DefaultPollingIterations = 12000;
    ULONG DefaultPollingIterationsMaximum = 12000;
    ULONG DefaultResendIterations = 0x3;
    ULONG DefaultSampleRate = 60;

    PAGED_CODE();

    RtlZeroMemory(Parameters, sizeof(Parameters));
    i = 0;

    Parameters[i].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Parameters[i].Name = L"Parameters";

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"KeyboardDataQueueSize";
    Parameters[i].EntryContext = &Settings->KeyboardDataQueueSize;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultKeyboardDataQueueSize;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"KeyboardDeviceBaseName";
    Parameters[i].EntryContext = &Settings->KeyboardDeviceBaseName;
    Parameters[i].DefaultType = REG_SZ;
    Parameters[i].DefaultData = (PVOID)DefaultKeyboardDeviceBaseName;
    Parameters[i].DefaultLength = 0;

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"MouseDataQueueSize";
    Parameters[i].EntryContext = &Settings->MouseDataQueueSize;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultMouseDataQueueSize;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"MouseResolution";
    Parameters[i].EntryContext = &Settings->MouseResolution;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultMouseResolution;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"MouseSynchIn100ns";
    Parameters[i].EntryContext = &Settings->MouseSynchIn100ns;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultMouseSynchIn100ns;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"NumberOfButtons";
    Parameters[i].EntryContext = &Settings->NumberOfButtons;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultNumberOfButtons;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"PointerDeviceBaseName";
    Parameters[i].EntryContext = &Settings->PointerDeviceBaseName;
    Parameters[i].DefaultType = REG_SZ;
    Parameters[i].DefaultData = (PVOID)DefaultPointerDeviceBaseName;
    Parameters[i].DefaultLength = 0;

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"PollStatusIterations";
    Parameters[i].EntryContext = &Settings->PollStatusIterations;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultPollStatusIterations;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"OverrideKeyboardType";
    Parameters[i].EntryContext = &Settings->OverrideKeyboardType;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultOverrideKeyboardType;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"OverrideKeyboardSubtype";
    Parameters[i].EntryContext = &Settings->OverrideKeyboardSubtype;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultOverrideKeyboardSubtype;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"PollingIterations";
    Parameters[i].EntryContext = &Settings->PollingIterations;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultPollingIterations;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"PollingIterationsMaximum";
    Parameters[i].EntryContext = &Settings->PollingIterationsMaximum;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultPollingIterationsMaximum;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"ResendIterations";
    Parameters[i].EntryContext = &Settings->ResendIterations;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultResendIterations;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"SampleRate";
    Parameters[i].EntryContext = &Settings->SampleRate;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultSampleRate;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
                                    RegistryPath->Buffer,
                                    Parameters,
                                    NULL,
                                    NULL);
    if (NT_SUCCESS(Status))
    {
        /* Check values */
        if (Settings->KeyboardDataQueueSize < 1)
            Settings->KeyboardDataQueueSize = DefaultKeyboardDataQueueSize;
        if (Settings->MouseDataQueueSize < 1)
            Settings->MouseDataQueueSize = DefaultMouseDataQueueSize;
        if (Settings->NumberOfButtons < 1)
            Settings->NumberOfButtons = DefaultNumberOfButtons;
        if (Settings->PollingIterations < 0x400)
            Settings->PollingIterations = DefaultPollingIterations;
        if (Settings->PollingIterationsMaximum < 0x400)
            Settings->PollingIterationsMaximum = DefaultPollingIterationsMaximum;
        if (Settings->ResendIterations < 1)
            Settings->ResendIterations = DefaultResendIterations;
    }
    else if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        /* Registry path doesn't exist. Set defaults */
        Settings->KeyboardDataQueueSize = DefaultKeyboardDataQueueSize;
        Settings->MouseDataQueueSize = DefaultMouseDataQueueSize;
        Settings->MouseResolution = DefaultMouseResolution;
        Settings->MouseSynchIn100ns = DefaultMouseSynchIn100ns;
        Settings->NumberOfButtons = DefaultNumberOfButtons;
        Settings->PollStatusIterations = DefaultPollStatusIterations;
        Settings->OverrideKeyboardType = DefaultOverrideKeyboardType;
        Settings->OverrideKeyboardSubtype = DefaultOverrideKeyboardSubtype;
        Settings->PollingIterations = DefaultPollingIterations;
        Settings->PollingIterationsMaximum = DefaultPollingIterationsMaximum;
        Settings->ResendIterations = DefaultResendIterations;
        Settings->SampleRate = DefaultSampleRate;
        if (!RtlCreateUnicodeString(&Settings->KeyboardDeviceBaseName, DefaultKeyboardDeviceBaseName)
         || !RtlCreateUnicodeString(&Settings->PointerDeviceBaseName, DefaultPointerDeviceBaseName))
        {
            WARN_(I8042PRT, "RtlCreateUnicodeString() failed\n");
            Status = STATUS_NO_MEMORY;
        }
        else
        {
            Status = STATUS_SUCCESS;
        }
    }

    if (NT_SUCCESS(Status))
    {
        INFO_(I8042PRT, "KeyboardDataQueueSize : 0x%lx\n", Settings->KeyboardDataQueueSize);
        INFO_(I8042PRT, "KeyboardDeviceBaseName : %wZ\n", &Settings->KeyboardDeviceBaseName);
        INFO_(I8042PRT, "MouseDataQueueSize : 0x%lx\n", Settings->MouseDataQueueSize);
        INFO_(I8042PRT, "MouseResolution : 0x%lx\n", Settings->MouseResolution);
        INFO_(I8042PRT, "MouseSynchIn100ns : %lu\n", Settings->MouseSynchIn100ns);
        INFO_(I8042PRT, "NumberOfButtons : 0x%lx\n", Settings->NumberOfButtons);
        INFO_(I8042PRT, "PointerDeviceBaseName : %wZ\n", &Settings->PointerDeviceBaseName);
        INFO_(I8042PRT, "PollStatusIterations : 0x%lx\n", Settings->PollStatusIterations);
        INFO_(I8042PRT, "OverrideKeyboardType : 0x%lx\n", Settings->OverrideKeyboardType);
        INFO_(I8042PRT, "OverrideKeyboardSubtype : 0x%lx\n", Settings->OverrideKeyboardSubtype);
        INFO_(I8042PRT, "PollingIterations : 0x%lx\n", Settings->PollingIterations);
        INFO_(I8042PRT, "PollingIterationsMaximum : %lu\n", Settings->PollingIterationsMaximum);
        INFO_(I8042PRT, "ResendIterations : 0x%lx\n", Settings->ResendIterations);
        INFO_(I8042PRT, "SampleRate : %lu\n", Settings->SampleRate);
    }

    i8042ServiceDebugSupport(RegistryPath, Settings);

    return Status;
}
