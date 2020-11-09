/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ARC path to-and-from NT path resolver.
 * COPYRIGHT:   Copyright 2017-2020 Hermes Belusca-Maito
 */
/*
 * References:
 *
 * - ARC Specification v1.2: http://netbsd.org./docs/Hardware/Machines/ARC/riscspec.pdf
 * - "Setup and Startup", MSDN article: https://technet.microsoft.com/en-us/library/cc977184.aspx
 * - Answer for "How do I determine the ARC path for a particular drive letter in Windows?": https://serverfault.com/a/5929
 * - ARC - LinuxMIPS: https://www.linux-mips.org/wiki/ARC
 * - ARCLoad - LinuxMIPS: https://www.linux-mips.org/wiki/ARCLoad
 * - Inside Windows 2000 Server: https://books.google.fr/books?id=kYT7gKnwUQ8C&pg=PA71&lpg=PA71&dq=nt+arc+path&source=bl&ots=K8I1F_KQ_u&sig=EJq5t-v2qQk-QB7gNSREFj7pTVo&hl=en&sa=X&redir_esc=y#v=onepage&q=nt%20arc%20path&f=false
 * - Inside Windows Server 2003: https://books.google.fr/books?id=zayrcM9ZYdAC&pg=PA61&lpg=PA61&dq=arc+path+to+nt+path&source=bl&ots=x2JSWfp2MA&sig=g9mufN6TCOrPejDov6Rjp0Jrldo&hl=en&sa=X&redir_esc=y#v=onepage&q=arc%20path%20to%20nt%20path&f=false
 *
 * Stuff to read: http://www.adminxp.com/windows2000/index.php?aid=46 and http://www.trcb.com/Computers-and-Technology/Windows-XP/Windows-XP-ARC-Naming-Conventions-1432.htm
 * concerning which values of disk() or rdisk() are valid when either scsi() or multi() adapters are specified.
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include "filesup.h"
#include "partlist.h"
#include "arcname.h"

#define NDEBUG
#include <debug.h>


/* TYPEDEFS *****************************************************************/

/* Supported adapter types */
typedef enum _ADAPTER_TYPE
{
    EisaAdapter,
    ScsiAdapter,
    MultiAdapter,
    NetAdapter,
    RamdiskAdapter,
    AdapterTypeMax
} ADAPTER_TYPE, *PADAPTER_TYPE;

/* Supported controller types */
typedef enum _CONTROLLER_TYPE
{
    DiskController,
    CdRomController,
    ControllerTypeMax
} CONTROLLER_TYPE, *PCONTROLLER_TYPE;

/* Supported peripheral types */
typedef enum _PERIPHERAL_TYPE
{
//  VDiskPeripheral, // Enable this when we'll support boot from virtual disks!
    RDiskPeripheral,
    FDiskPeripheral,
    CdRomPeripheral,
    PeripheralTypeMax
} PERIPHERAL_TYPE, *PPERIPHERAL_TYPE;


/* PARSER - UNICODE MODE ****************************************************/

#define  UNICODE
#define _UNICODE
#include "arcnameAU.c"


/* FUNCTIONS ****************************************************************/

/*
 * ArcName:
 *      ARC name (counted string) to be resolved into a NT device name.
 *      The caller should have already delimited it from within an ARC path
 *      (usually by finding where the first path separator appears in the path).
 *
 * NtName:
 *      Receives the resolved NT name. The buffer is NULL-terminated.
 */
static NTSTATUS
ResolveArcNameNtSymLink(
    OUT PUNICODE_STRING NtName,
    IN  PUNICODE_STRING ArcName)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE DirectoryHandle, LinkHandle;
    UNICODE_STRING ArcNameDir;

    if (NtName->MaximumLength < sizeof(UNICODE_NULL))
        return STATUS_BUFFER_TOO_SMALL;

    /* Open the \ArcName object directory */
    RtlInitUnicodeString(&ArcNameDir, L"\\ArcName");
    InitializeObjectAttributes(&ObjectAttributes,
                               &ArcNameDir,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenDirectoryObject(&DirectoryHandle,
                                   DIRECTORY_ALL_ACCESS,
                                   &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenDirectoryObject(%wZ) failed, Status 0x%08lx\n", &ArcNameDir, Status);
        return Status;
    }

    /* Open the ARC name link */
    InitializeObjectAttributes(&ObjectAttributes,
                               ArcName,
                               OBJ_CASE_INSENSITIVE,
                               DirectoryHandle,
                               NULL);
    Status = NtOpenSymbolicLinkObject(&LinkHandle,
                                      SYMBOLIC_LINK_QUERY,
                                      &ObjectAttributes);

    /* Close the \ArcName object directory handle */
    NtClose(DirectoryHandle);

    /* Check for success */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenSymbolicLinkObject(%wZ) failed, Status 0x%08lx\n", ArcName, Status);
        return Status;
    }

    /* Reserve one WCHAR for the NULL-termination */
    NtName->MaximumLength -= sizeof(UNICODE_NULL);

    /* Resolve the link and close its handle */
    Status = NtQuerySymbolicLinkObject(LinkHandle, NtName, NULL);
    NtClose(LinkHandle);

    /* Restore the NULL-termination */
    NtName->MaximumLength += sizeof(UNICODE_NULL);

    /* Check for success */
    if (!NT_SUCCESS(Status))
    {
        /* We failed, don't touch NtName */
        DPRINT1("NtQuerySymbolicLinkObject(%wZ) failed, Status 0x%08lx\n", ArcName, Status);
    }
    else
    {
        /* We succeeded, NULL-terminate NtName */
        NtName->Buffer[NtName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    return Status;
}

/*
 * ArcNamePath:
 *      In input, pointer to an ARC path (NULL-terminated) starting by an
 *      ARC name to be resolved into a NT device name.
 *      In opposition to ResolveArcNameNtSymLink(), the caller does not have
 *      to delimit the ARC name from within an ARC path. The real ARC name is
 *      deduced after parsing the ARC path, and, in output, ArcNamePath points
 *      to the beginning of the path after the ARC name part.
 *
 * NtName:
 *      Receives the resolved NT name. The buffer is NULL-terminated.
 *
 * PartList:
 *      (Optional) partition list that helps in resolving the paths pointing
 *      to hard disks.
 */
static NTSTATUS
ResolveArcNameManually(
    OUT PUNICODE_STRING NtName,
    IN OUT PCWSTR* ArcNamePath,
    IN  PPARTLIST PartList)
{
    NTSTATUS Status;
    ULONG AdapterKey;
    ULONG ControllerKey;
    ULONG PeripheralKey;
    ULONG PartitionNumber;
    ADAPTER_TYPE AdapterType;
    CONTROLLER_TYPE ControllerType;
    PERIPHERAL_TYPE PeripheralType;
    DEVICE_SIGNATURE Signature;
    SIZE_T NameLength;

    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry = NULL;

    if (NtName->MaximumLength < sizeof(UNICODE_NULL))
        return STATUS_BUFFER_TOO_SMALL;

    /* Parse the ARC path */
    Status = ParseArcNameU(ArcNamePath,
                           &AdapterKey,
                           &ControllerKey,
                           &PeripheralKey,
                           &PartitionNumber,
                           &AdapterType,
                           &ControllerType,
                           &PeripheralType,
                           &Signature);
    if (!NT_SUCCESS(Status))
        return Status;

    // TODO: Check the partition number in case of fdisks and cdroms??

    /* Check for adapters that don't take any extra controller or peripheral node */
    if (AdapterType == NetAdapter || AdapterType == RamdiskAdapter)
    {
        if (AdapterType == NetAdapter)
        {
            DPRINT1("%S(%lu) path is not supported!\n", AdapterTypes_U[AdapterType], AdapterKey);
            return STATUS_NOT_SUPPORTED;
        }

        Status = RtlStringCbPrintfW(NtName->Buffer, NtName->MaximumLength,
                                    L"\\Device\\Ramdisk%lu", AdapterKey);
    }
    else
    if (ControllerType == CdRomController) // and so, AdapterType == ScsiAdapter and PeripheralType == FDiskPeripheral
    {
        Status = RtlStringCbPrintfW(NtName->Buffer, NtName->MaximumLength,
                                    L"\\Device\\Scsi\\CdRom%lu", ControllerKey);
    }
    else
    /* Now, ControllerType == DiskController */
    if (PeripheralType == CdRomPeripheral)
    {
        Status = RtlStringCbPrintfW(NtName->Buffer, NtName->MaximumLength,
                                    L"\\Device\\CdRom%lu", PeripheralKey);
    }
    else
    if (PeripheralType == FDiskPeripheral)
    {
        Status = RtlStringCbPrintfW(NtName->Buffer, NtName->MaximumLength,
                                    L"\\Device\\Floppy%lu", PeripheralKey);
    }
    else
    if (PeripheralType == RDiskPeripheral)
    {
        if (Signature.Type != SignatureNone)
        {
            DiskEntry = GetDiskBySignature(PartList, Signature);
        }
        else
        {
            DiskEntry = GetDiskBySCSI(PartList, AdapterKey,
                                      ControllerKey, PeripheralKey);
        }
        if (!DiskEntry)
            return STATUS_OBJECT_PATH_NOT_FOUND; // STATUS_NOT_FOUND;

        if (PartitionNumber != 0)
        {
            PartEntry = GetPartition(DiskEntry, PartitionNumber);
            if (!PartEntry)
                return STATUS_OBJECT_PATH_NOT_FOUND; // STATUS_DEVICE_NOT_PARTITIONED;
            ASSERT(PartEntry->DiskEntry == DiskEntry);
        }

        Status = RtlStringCbPrintfW(NtName->Buffer, NtName->MaximumLength,
                                    L"\\Device\\Harddisk%lu\\Partition%lu",
                                    DiskEntry->DiskNumber, PartitionNumber);
    }
#if 0 // FIXME: Not implemented yet!
    else
    if (PeripheralType == VDiskPeripheral)
    {
        // TODO: Check how Win 7+ deals with virtual disks.
        Status = RtlStringCbPrintfW(NtName->Buffer, NtName->MaximumLength,
                                    L"\\Device\\VirtualHarddisk%lu\\Partition%lu",
                                    PeripheralKey, PartitionNumber);
    }
#endif

    if (!NT_SUCCESS(Status))
    {
        /* Returned NtName is invalid, so zero it out */
        *NtName->Buffer = UNICODE_NULL;
        NtName->Length = 0;

        return Status;
    }

    /* Update NtName length */
    NameLength = wcslen(NtName->Buffer);
    if (NameLength > UNICODE_STRING_MAX_CHARS)
    {
        return STATUS_NAME_TOO_LONG;
    }

    NtName->Length = (USHORT)NameLength * sizeof(WCHAR);

    return STATUS_SUCCESS;
}


BOOLEAN
ArcPathToNtPath(
    OUT PUNICODE_STRING NtPath,
    IN  PCWSTR ArcPath,
    IN  PPARTLIST PartList OPTIONAL)
{
    NTSTATUS Status;
    PCWSTR BeginOfPath;
    UNICODE_STRING ArcName;
    SIZE_T PathLength;

    /* TODO: We should "normalize" the path, i.e. expand all the xxx() into xxx(0) */

    if (NtPath->MaximumLength < sizeof(UNICODE_NULL))
        return FALSE;

    *NtPath->Buffer = UNICODE_NULL;
    NtPath->Length = 0;

    /*
     * - First, check whether the ARC path is already inside \\ArcName
     *   and if so, map it to the corresponding NT path.
     * - Only then, if we haven't found any ArcName, try to build a
     *   NT path by deconstructing the ARC path, using its disk and
     *   partition numbers. We may use here our disk/partition list.
     *
     * See also freeldr/arcname.c
     *
     * Note that it would be nice to maintain a cache of these mappings.
     */

    /*
     * Initialize the ARC name to resolve, by cutting the ARC path at the first
     * NT path separator. The ARC name therefore ends where the NT path part starts.
     */
    RtlInitUnicodeString(&ArcName, ArcPath);
    BeginOfPath = wcschr(ArcName.Buffer, OBJ_NAME_PATH_SEPARATOR);
    if (BeginOfPath)
        ArcName.Length = (ULONG_PTR)BeginOfPath - (ULONG_PTR)ArcName.Buffer;

    /* Resolve the ARC name via NT SymLinks. Note that NtPath is returned NULL-terminated. */
    Status = ResolveArcNameNtSymLink(NtPath, &ArcName);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, attempt a manual resolution */
        DPRINT1("ResolveArcNameNtSymLink(ArcName = '%wZ') for ArcPath = '%S' failed, Status 0x%08lx\n", &ArcName, ArcPath, Status);

        /*
         * We failed at directly resolving the ARC path, and we cannot perform
         * a manual resolution because we don't have any disk/partition list,
         * we therefore fail here.
         */
        if (!PartList)
        {
            DPRINT1("PartList == NULL, cannot perform a manual resolution\n");
            return FALSE;
        }

        *NtPath->Buffer = UNICODE_NULL;
        NtPath->Length = 0;

        BeginOfPath = ArcPath;
        Status = ResolveArcNameManually(NtPath, &BeginOfPath, PartList);
        if (!NT_SUCCESS(Status))
        {
            /* We really failed this time, bail out */
            DPRINT1("ResolveArcNameManually(ArcPath = '%S') failed, Status 0x%08lx\n", ArcPath, Status);
            return FALSE;
        }
    }

    /*
     * We succeeded. Concatenate the rest of the system-specific path. We know the path is going
     * to be inside the NT namespace, therefore we can use the path string concatenation function
     * that uses '\\' as the path separator.
     */
    if (BeginOfPath && *BeginOfPath)
    {
        Status = ConcatPaths(NtPath->Buffer, NtPath->MaximumLength / sizeof(WCHAR), 1, BeginOfPath);
        if (!NT_SUCCESS(Status))
        {
            /* Buffer not large enough, or whatever...: just bail out */
            return FALSE;
        }
    }

    PathLength = wcslen(NtPath->Buffer);
    if (PathLength > UNICODE_STRING_MAX_CHARS)
    {
        return FALSE;
    }

    NtPath->Length = (USHORT)PathLength * sizeof(WCHAR);

    return TRUE;
}

#if 0 // FIXME: Not implemented yet!
PWSTR
NtPathToArcPath(
    IN PWSTR NtPath)
{
    /*
     * - First, check whether any of the ARC paths inside \\ArcName
     *   map to the corresponding NT path. If so, we are OK.
     * - Only then, if we haven't found any ArcName, try to build an
     *   ARC path by deconstructing the NT path, using its disk and
     *   partition numbers. We may use here our disk/partition list.
     *
     * See also freeldr/arcname.c
     *
     * Note that it would be nice to maintain a cache of these mappings.
     */
}
#endif

/* EOF */
