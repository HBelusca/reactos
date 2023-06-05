/*
 * PROJECT:     ReactOS "Universal" Fallback Video Miniport Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tries to load a fallback video miniport driver at run-time.
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <ntdef.h>
//#define PAGE_SIZE 4096
//   #include <dderror.h>
//   //#include "devioctl.h"
//   #include <miniport.h>
//   #include <video.h>

#include <debug.h>
#include <dpfilter.h>

/* FUNCTIONS ******************************************************************/

RTL_QUERY_REGISTRY_ROUTINE UniVmpTryFallbackDriver;

_Use_decl_annotations_
NTSTATUS
UniVmpTryFallbackDriver(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
#define MAX_KEY_LENGTH  0x200
    static const UNICODE_STRING ServicesKeyPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\" REGSTR_PATH_SERVICES L"\\");

    UNICODE_STRING DriverName;
    UNICODE_STRING DriverServiceName;
    CHAR Buffer[MAX_KEY_LENGTH];

    if (ValueType != REG_SZ)
        return STATUS_OBJECT_TYPE_MISMATCH;

    /* Initialize the video miniport driver name */
    DriverName.Length = DriverName.MaximumLength = ValueLength;
    DriverName.Buffer = ValueData;

    /* Create the full service key for this driver */
    RtlInitEmptyUnicodeString(&DriverServiceName, (PWCH)Buffer, sizeof(Buffer));
    RtlCopyUnicodeString(&DriverServiceName, &ServicesKeyPath);
    RtlAppendUnicodeStringToString(&DriverServiceName, &DriverName);

    /* Ask the kernel to load it for us */
    // Status = ZwLoadDriver(&DriverServiceName);
    Status = STATUS_SUCCESS;

    ERR_(VIDEOPRT, "TryFallbackDriver: Trying loading driver '%wZ' ('%wZ')\n",
         &DriverName, &DriverServiceName);

    return Status;
}

ULONG
NTAPI
DriverEntry(
    _In_ PVOID Context1,
    _In_ PVOID Context2)
{
    NTSTATUS Status;
    PDRIVER_OBJECT DriverObject = Context1;
    PUNICODE_STRING RegistryPath = Context2;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE DriverKeyHandle;
    RTL_QUERY_REGISTRY_TABLE QueryTable[2] = {{0}};

    /* Initialize the object attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               RegistryPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&DriverKeyHandle,
                       KEY_QUERY_VALUE/*GENERIC_READ*/,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        ERR_(VIDEOPRT, "DriverEntry: Could not open '%wZ'\n", RegistryPath);
        return Status;
    }

    /* Enumerate the fallback video miniport drivers to test */
    QueryTable[0].QueryRoutine = UniVmpTryFallbackDriver;
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_REQUIRED;
    QueryTable[0].Name = L"FallbackDrivers";
    // PVOID EntryContext;
    // QueryTable[0].DefaultType = REG_NONE;
    // PVOID DefaultData;
    // ULONG DefaultLength;

    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_HANDLE,
                                    (PCWSTR)DriverKeyHandle,
                                    QueryTable,
                                    NULL, // &Context,
                                    NULL);

    ZwClose(DriverKeyHandle);

    if (!NT_SUCCESS(Status))
    {
        /*WARN_*/ERR_(VIDEOPRT, "DriverEntry: Could not find a fallback video driver!\n");
        return Status;
    }

    return Status;
}

/* EOF */
