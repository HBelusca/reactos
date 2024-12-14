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

NTSTATUS
ReadRegistryEntries(
    IN PUNICODE_STRING RegistryPath,
    OUT PI8042_SETTINGS Settings)
{
    NTSTATUS Status;
    ULONG i;
    RTL_QUERY_REGISTRY_TABLE Parameters[19];

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

    ULONG DefaultBreakOnSysRq = 1;

    /* Default values depending on whether we are
     * running a debug build or a normal build. */
#if DBG
    ULONG DefaultKdEnableOnCtrlSysRq = 1;
    ULONG DefaultCrashOnCtrlScroll = 1;
#else
    ULONG DefaultKdEnableOnCtrlSysRq = 0;
    ULONG DefaultCrashOnCtrlScroll = 0;
#endif

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

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"BreakOnSysRq";
    Parameters[i].EntryContext = &Settings->BreakOnSysRq;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultBreakOnSysRq;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"KdEnableOnCtrlSysRq";
    Parameters[i].EntryContext = &Settings->KdEnableOnCtrlSysRq;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultKdEnableOnCtrlSysRq;
    Parameters[i].DefaultLength = sizeof(ULONG);

    Parameters[++i].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Parameters[i].Name = L"CrashOnCtrlScroll";
    Parameters[i].EntryContext = &Settings->CrashOnCtrlScroll;
    Parameters[i].DefaultType = REG_DWORD;
    Parameters[i].DefaultData = &DefaultCrashOnCtrlScroll;
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
        Settings->BreakOnSysRq = DefaultBreakOnSysRq;
        Settings->KdEnableOnCtrlSysRq = DefaultKdEnableOnCtrlSysRq;
        Settings->CrashOnCtrlScroll = DefaultCrashOnCtrlScroll;
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

    return Status;
}
