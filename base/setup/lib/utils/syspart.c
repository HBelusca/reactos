/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Firmware System Partition helpers
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *
 * REFERENCES:
 * http://jdebp.uk./FGA/boot-and-system-volumes.html
 * http://jdebp.uk./FGA/pcat-boot-process.html
 * https://jdebp.uk/FGA/arc-boot-process.html
 * https://jdebp.uk/FGA/efi-boot-process.html
 * http://jdebp.uk./FGA/determining-system-volume.html
 */

#include "precomp.h"

#include "partlist.h"
#include "volutil.h"
/// #include "fsrec.h" // For FileSystemToMBRPartitionType()

#include <ndk/exfuncs.h> // For Nt(Query/Set)SystemEnvironmentValue()

#define NDEBUG
#include <debug.h>


// See ex/sysinfo.c
/* The maximum size of an environment value (in bytes) */
#define MAX_ENVVAL_SIZE 1024


/* FUNCTIONS ****************************************************************/

//
// TODO:
//
// 1. An OS-specific function that determines **THE CURRENT** system partition
//    the operating system booted from. (With NT, the HKLM\System\Setup
//    "SystemPartition" value set by the kernel.)
//
// 2. A disk-layout-specific function determining whether a given partition
//    is marked as system (see IsPartitionActive(); IsSystemPartition()).
//    There can be more than one such marked partition on any given disk.
//
// 3. Optionally, a disk-layout-specific function determining whether a given
//    partition is marked as a service partition.
//
// 4. A platform-specific (BIOS-based PC, ARC, (U)EFI...) function that
//    determines whether a given partition is a system partition.
//    For BIOS-based PC or (U)EFI systems, this is identical to 2.
//    For ARC, we may rely on 2., but not necessarily. Instead we check
//    the environment variables "SystemPartition", "FWSearchPath"
//    (semicolon-separated values) (the latter is "Firmware Search Path Environment Variable")
//
// 5. A pair of OS-specific functions to unprotect and protect a system
//    partition for write-access (used when installing a bootloader).
//


/**
 * @brief
 * Retrieve the system disk, i.e. the fixed disk that is accessible by the
 * firmware during boot time and where the system partition resides.
 * If no system partition has been determined, we retrieve the first disk
 * that verifies the mentioned criteria above.
 *
 * **BIOS-based platform specific** -- Retrieves the bootable disk by the BIOS.
 **/
//static
PDISKENTRY
GetSystemDisk(
    _In_ PPARTLIST List)
{
    PDISKENTRY DiskEntry;

    /* Check for empty disk list */
    if (IsListEmpty(&List->DiskListHead))
        return NULL;

    /*
     * If we already have a system partition, the system disk
     * is the one on which the system partition resides.
     */
    if (List->SystemPartition)
        return List->SystemPartition->DiskEntry;

    /* Loop over the disks and find the correct one */
    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDisk(&List->DiskListHead, DiskEntry, TRUE)))
    {
        /* The disk must be a fixed disk and be found by the firmware */
        if (DiskEntry->MediaType == FixedMedia && DiskEntry->BiosFound)
            break;
    }
    if (!DiskEntry)
        return NULL; /* No suitable disk found */

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        DPRINT1("System disk -- GPT-partitioned disk detected, not currently supported by SETUP!\n");

    return DiskEntry;
}

/**
 * @brief
 * Retrieve the first system partition on the given disk
 * (or the single one on BIOS-based PCs).
 **/
//static
PPARTENTRY
GetActiveDiskPartition(
    _In_ PDISKENTRY DiskEntry)
{
    PPARTENTRY PartEntry;

    ASSERT(DiskEntry);

    /* Check for empty partition list */
    if (IsListEmpty(&DiskEntry->PrimaryPartListHead))
        return NULL;

    /* Scan all (primary) partitions to find the first active disk partition */
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartition(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
    {
        if (PartEntry->IsSystemPartition)
        {
            /* Yes, we've found it */
            PVOLINFO VolInfo = (PartEntry->Volume ? &PartEntry->Volume->Info : NULL);

            ASSERT(DiskEntry == PartEntry->DiskEntry);
            ASSERT(PartEntry->IsPartitioned);
            //ASSERT(PartEntry->Volume);

            DPRINT1("Found active system partition %lu in disk %lu, drive %C%C\n",
                    PartEntry->PartitionNumber, DiskEntry->DiskNumber,
                    !(VolInfo && VolInfo->DriveLetter) ? L'-' : VolInfo->DriveLetter,
                    !(VolInfo && VolInfo->DriveLetter) ? L'-' : L':');
            break;
        }
    }

    /* Check if the disk is new and if so, use its first partition as the active system partition */
    if (DiskEntry->NewDisk && PartEntry)
    {
        // FIXME: What to do??
        DPRINT1("NewDisk TRUE but already existing active partition?\n");
    }

    /* Return the active partition found (or none) */
    return PartEntry;
}


#if 0
// Needs the SeSystemEnvironmentPrivilege privilege.
// Converts the name and value to ANSI and calls HalSetEnvironmentVariable().
NTSTATUS
NTAPI
NtSetSystemEnvironmentValue(IN PUNICODE_STRING VariableName,
                            IN PUNICODE_STRING Value);

// Needs the SeSystemEnvironmentPrivilege privilege.
// Converts the name and value to ANSI and calls HalGetEnvironmentVariable().
NTSTATUS
NTAPI
NtQuerySystemEnvironmentValue(IN PUNICODE_STRING VariableName,
                              OUT PWSTR ValueBuffer,
                              IN ULONG ValueBufferLength,
                              IN OUT PULONG ReturnLength OPTIONAL);
#endif

/**
 * @brief
 * Retrieves the value of an environment variable (ARC platforms),
 * split it at every semi-colon ';' separator, and returns a
 * multi-string.
 *
 * **NOTE** that successive separators are collapsed, thus, the
 * resulting buffer cannot serve to reconstruct the original string
 * or serve as indexing the separate values.
 **/
NTSTATUS
SpArcGetEnvironmentValues(
    _In_ PCWSTR VariableName,
    _Out_writes_(BufferLength) PZZWSTR ValuesBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    ULONG ValueLength = 0;
    PWSTR Buffer;
    PWCHAR Ptr, End;
    BOOLEAN PrivAdjusted, OldState = FALSE;

    /*
     * Retrieve the firmware environment value.
     * Do the call under the SeSystemEnvironmentPrivilege privilege.
     * If we couldn't acquire it, the call will fail.
     */
    Status = RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &OldState);
    PrivAdjusted = NT_SUCCESS(Status);
    if (!PrivAdjusted)
        DPRINT1("Cannot acquire SeSystemEnvironmentPrivilege, Status = 0x%08lx\n", Status);

    RtlInitUnicodeString(&Name, VariableName);
    Status = NtQuerySystemEnvironmentValue(&Name,
                                           ValuesBuffer,
                                           BufferLength - sizeof(UNICODE_NULL), // Reserve space for one extra NUL.
                                           &ValueLength);

    if (PrivAdjusted)
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, OldState, FALSE, &OldState);

    if (ReturnLength)
        *ReturnLength = ValueLength;
    if (!NT_SUCCESS(Status))
        return Status;

    /* Build the multi-string */
    End = ValuesBuffer + (ValueLength / sizeof(WCHAR));
    for (Ptr = Buffer = ValuesBuffer; Ptr < End; ++Ptr)
    {
        /* Skip any consecutive separators */
        while (Ptr < End && (*Ptr == L';'))
            ++Ptr;

        /* Copy everything until the next separator */
        while (Ptr < End && (*Ptr != L';'))
            *Buffer++ = *Ptr++;
        /* NUL at the separator */
        *Buffer++ = UNICODE_NULL;
    }
    /* Append the final NUL-terminator */
    *Buffer = UNICODE_NULL;

    if (ReturnLength)
        *ReturnLength = (ULONG_PTR)Buffer - (ULONG_PTR)ValuesBuffer;
    return STATUS_SUCCESS;
}



/**
 * @brief
 * Tells whether the partition is marked as a system partition.
 * Partition with the Active/Boot flag set.
 **/
inline
BOOLEAN
MBRIsSystemPartition(
    _In_ PPARTITION_INFORMATION PartitionInfo)
{
    /* Check whether the partition is marked "active" */
    return (/*PartEntry->IsPartitioned &&*/
            (PartitionInfo->PartitionType != PARTITION_ENTRY_UNUSED) &&
            !IsContainerPartition(PartitionInfo->PartitionType) &&
            PartitionInfo->BootIndicator);
}

/**
 * @brief
 * Tells whether the partition is marked as a system partition.
 * Partition with the PARTITION_SYSTEM_GUID.
 **/
inline
BOOLEAN
GPTIsSystemPartition(
    _In_ PPARTITION_INFORMATION_GPT Info)
{
    /* Check whether the partition has the System GUID */
    // NOTE: Here, Info->Attributes may have GPT_ATTRIBUTE_PLATFORM_REQUIRED.
    return (/*PartEntry->IsPartitioned &&*/
            IsEqualGUID(Info->PartitionType, PARTITION_SYSTEM_GUID));
}


/**
 * @brief
 * Marks a partition as "system" on an MBR disk (i.e. "active" partition).
 **/
VOID
MBRSetSystemPartition(
    _In_ PPARTENTRY PartEntry,
    _In_ BOOLEAN SetSystem)
{
    ASSERT(PartEntry->IsPartitioned);
    ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

    if (!!SetSystem == !!PartEntry->IsSystemPartition)
        return;

    /* Set or unset the active partition */
    PartEntry->IsSystemPartition = !!SetSystem;
    PartEntry->DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].BootIndicator = !!SetSystem;
    PartEntry->DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].RewritePartition = TRUE;
    PartEntry->DiskEntry->Dirty = TRUE;
}

/**
 * @brief
 * Marks a partition as "system" on a GPT disk.
 **/
VOID
GPTSetSystemPartition(
    _In_ PPARTENTRY PartEntry,
    _In_ BOOLEAN SetSystem)
{
    ASSERT(PartEntry->IsPartitioned);
    // ASSERT(!IsEqualGUID(PartEntry->Info->PartitionType, PARTITION_ENTRY_UNUSED_GUID));

    if (!!SetSystem == !!PartEntry->IsSystemPartition)
        return;

    // TODO: Change the partition GUID to PARTITION_SYSTEM_GUID
    // and add the GPT_ATTRIBUTE_PLATFORM_REQUIRED bit.
    UNIMPLEMENTED;
}


/**
 * @brief
 * Set a partition as "system" on a BIOS-based platform.
 **/
VOID
PtBiosSetSystemPartition(
    _In_ PPARTENTRY PartEntry)
{
    PPARTENTRY OldActivePart;

    ASSERT(PartEntry->IsPartitioned);
    if (PartEntry->IsSystemPartition)
        return;

    /* Determine the current active partition on
     * the disk where the new partition belongs. */
    // TODO Maybe improvement?? Really ensure there can be only ONE
    // system / "active" partition on the disk, by retrieving ALL
    // the hypothetical active partitions on that disk and unsetting them.
    // (In principle this should not happen, but we never know.)
    OldActivePart = GetActiveDiskPartition(PartEntry->DiskEntry);

    /* If the partition entry is already the system partition, or if
     * it is the same as the old active partition, just return success. */
    if (PartEntry == OldActivePart)
    {
        ASSERT(OldActivePart->IsSystemPartition);
        ASSERT(OldActivePart->DiskEntry->LayoutBuffer->PartitionEntry[OldActivePart->PartitionIndex].BootIndicator);
        return;
    }

    /* Unset the old active partition if it exists,
     * and set the new active partition */
    if (OldActivePart)
        MBRSetSystemPartition(OldActivePart, FALSE);
    MBRSetSystemPartition(PartEntry, TRUE);

    // /* Modify the system partition if the new partition is on the system disk */
    // PPARTLIST List = PartEntry->DiskEntry->PartList;
    // if (PartEntry->DiskEntry == GetSystemDisk(List))
    //     List->SystemPartition = PartEntry;
}

/**
 * @brief
 * Set a partition as "system" on a (U)EFI platform.
 **/
VOID
PtEfiSetSystemPartition(
    _In_ PPARTENTRY PartEntry)
{
    PARTITION_STYLE DiskStyle = PartEntry->DiskEntry->DiskStyle;

    ASSERT(PartEntry->IsPartitioned);
    if (PartEntry->IsSystemPartition)
        return;

    if (DiskStyle == PARTITION_STYLE_MBR)
    {
        /* Mark the partition with type 0xEF ("UEFI System Partition") */
        // MBRSetSystemPartition(PartEntry, TRUE);
        SetMBRPartitionType(PartEntry, 0xEF);
    }
    else if (DiskStyle == PARTITION_STYLE_GPT)
    {
        GPTSetSystemPartition(PartEntry, TRUE);
    }
    else
    {
        DPRINT1("Skipping unrecognized disk of style %lu\n", DiskStyle);
    }
}

/**
 * @brief
 * Set a partition as "system" on an ARC platform.
 **/
VOID
PtArcSetSystemPartition(
    _In_ PPARTENTRY PartEntry)
{
    ASSERT(PartEntry->IsPartitioned);
    if (PartEntry->IsSystemPartition)
        return;

    /* Nothing to do. This is done when adding boot entries. At most,
     * one could add it to the "FWSearchPath" environment variable. */
    PartEntry->IsSystemPartition = TRUE;
}

VOID
PtSetSystemPartition(
    _In_ PPARTENTRY PartEntry)
{
    // if ((ArchType == ARCH_PcAT) || (ArchType == ARCH_NEC98x86))
    return PtBiosSetSystemPartition(PartEntry);
    // else if (ArchType == ARCH_Arc)
        // return PtArcSetSystemPartition(PartEntry);
    // else if (ArchType == ARCH_Efi)
        // return PtEfiSetSystemPartition(PartEntry);
    // else
        // return STATUS_NOT_SUPPORTED;
}


/**
 * @brief
 * Finds registered system partitions suitable for BIOS-based platforms.
 *
 * On BIOS-based platforms, there can be only one marked system partition
 * on a given disk.
 **/
NTSTATUS
PtBiosMarkSystemPartitions(
    _In_ PPARTLIST PartList)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    PARTITION_STYLE DiskStyle;
    BOOLEAN AlreadyActive;

    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDisk(&PartList->DiskListHead, DiskEntry, TRUE)))
    {
        DiskStyle = DiskEntry->DiskStyle;

        if (!(DiskEntry->MediaType == FixedMedia && DiskEntry->BiosFound))
        {
            DPRINT1("Skipping non-fixed or non-firmware-found disk 0x%p\n", DiskEntry);
            continue;
        }

        /* Ignore if the disk is not MBR or GPT */
        if (DiskStyle != PARTITION_STYLE_MBR &&
            DiskStyle != PARTITION_STYLE_GPT)
        {
            DPRINT1("Skipping unrecognized disk of style %lu\n", DiskStyle);
            continue;
        }

        /* No already active partition on this disk for now */
        AlreadyActive = FALSE;

        PartEntry = NULL;
        while ((PartEntry = GetAdjPartition(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
        {
            ASSERT(PartEntry->DiskEntry == DiskEntry);

            if (!PartEntry->IsPartitioned)
                continue;

            // PartEntry->IsSystemPartition = FALSE;
            if (DiskStyle == PARTITION_STYLE_MBR)
            {
                PartEntry->IsSystemPartition =
                    MBRIsSystemPartition(&DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex]);
            }
            else // if (DiskStyle == PARTITION_STYLE_GPT)
            {
                PartEntry->IsSystemPartition = FALSE;
                    // GPTIsSystemPartition(&DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex]);
            }

            if (PartEntry->IsSystemPartition)
            {
                /* DPRINT1 a warning if there is more than
                 * one active partition found on this disk */
                if (AlreadyActive)
                    DPRINT1("WARNING: More than one active partition found on this MBR disk!\n");

                AlreadyActive = TRUE;
            }
        }
    }

    return STATUS_SUCCESS;
}


/**
 * @brief
 * Finds registered system partitions suitable for (U)EFI-based platforms.
 **/
NTSTATUS
PtEfiMarkSystemPartitions(
    _In_ PPARTLIST PartList)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    PARTITION_STYLE DiskStyle;

    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDisk(&PartList->DiskListHead, DiskEntry, TRUE)))
    {
        DiskStyle = DiskEntry->DiskStyle;

        if (!(DiskEntry->MediaType == FixedMedia && DiskEntry->BiosFound))
        {
            DPRINT1("Skipping non-fixed or non-firmware-found disk 0x%p\n", DiskEntry);
            continue;
        }

        /* Ignore if the disk is not MBR or GPT */
        if (DiskStyle != PARTITION_STYLE_MBR &&
            DiskStyle != PARTITION_STYLE_GPT)
        {
            DPRINT1("Skipping unrecognized disk of style %lu\n", DiskStyle);
            continue;
        }

        PartEntry = NULL;
        while ((PartEntry = GetAdjPartition(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
        {
            ASSERT(PartEntry->DiskEntry == DiskEntry);

            if (!PartEntry->IsPartitioned)
                continue;

            // PartEntry->IsSystemPartition = FALSE;
            if (DiskStyle == PARTITION_STYLE_MBR)
            {
                /* Check for partitions with type 0xEF ("UEFI System Partition") */
                PartEntry->IsSystemPartition = (PartEntry->PartitionType == 0xEF);
            }
            else // if (DiskStyle == PARTITION_STYLE_GPT)
            {
                /* Check for partitions with System GUID */
                PartEntry->IsSystemPartition =
                    GPTIsSystemPartition(&DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex]);
            }
        }
    }

    return STATUS_SUCCESS;
}


/**
 * @brief
 * Finds registered system partitions suitable for ARC platforms.
 **/
NTSTATUS
PtArcMarkSystemPartitions(
    _In_ PPARTLIST PartList)
{
    NTSTATUS Status;
    PWSTR EnvValue;
    ULONG ReturnLength;
    WCHAR EnvBuffer[MAX_ENVVAL_SIZE + 1]; // Reserve space for one extra NUL.

    ULONG AdapterKey;
    ULONG ControllerKey;
    ULONG PeripheralKey;
    ULONG PartitionNumber;
    ADAPTER_TYPE AdapterType;
    CONTROLLER_TYPE ControllerType;
    PERIPHERAL_TYPE PeripheralType;
    BOOLEAN UseSignature;
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;

    Status = SpArcGetEnvironmentValues(L"SystemPartition",
                                       EnvBuffer,
                                       sizeof(EnvBuffer),
                                       &ReturnLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ReturnLength > sizeof(EnvBuffer))
        return STATUS_BUFFER_OVERFLOW;

    for (EnvValue = EnvBuffer; *EnvValue; EnvValue += wcslen(EnvValue) + 1)
    {
        PWSTR ArcNamePath = EnvValue;

        DPRINT1("Value: '%S'\n", EnvValue);

        /* Parse the ARC path */
        Status = ParseArcName(ArcNamePath,
                              &AdapterKey,
                              &ControllerKey,
                              &PeripheralKey,
                              &PartitionNumber,
                              &AdapterType,
                              &ControllerType,
                              &PeripheralType,
                              &UseSignature);
        if (!NT_SUCCESS(Status))
        {
            /* Skip the value */
            DPRINT1("Unknown ARC path '%S'\n", EnvValue);
            continue;
        }

        /* Only handle fixed disks (RDisk) */
        if (PeripheralType != RDiskPeripheral)
            continue;

        if (UseSignature)
        {
            /* The disk signature is stored in AdapterKey */
            DiskEntry = GetDiskBySignature(PartList, AdapterKey);
        }
        else
        {
            DiskEntry = GetDiskBySCSI(PartList, AdapterKey,
                                      ControllerKey, PeripheralKey);
        }
        if (!DiskEntry)
        {
            DPRINT1("Disk not found '%S'\n", EnvValue);
            continue;
        }

        PartEntry = NULL;
        if (PartitionNumber != 0)
        {
            PartEntry = GetPartition(DiskEntry, PartitionNumber);
            if (!PartEntry)
            {
                DPRINT1("Partition not found '%S'\n", EnvValue);
                continue;
            }
            ASSERT(PartEntry->DiskEntry == DiskEntry);
        }

        /* Mark the system partition */
        if (PartEntry)
            PartEntry->IsSystemPartition = TRUE;
    }

#if 0
    Status = SpArcGetEnvironmentValues(L"FWSearchPath",
                                       EnvBuffer,
                                       sizeof(EnvBuffer),
                                       &ReturnLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ReturnLength > sizeof(EnvBuffer))
        return STATUS_BUFFER_OVERFLOW;

    for (EnvValue = EnvBuffer; *EnvValue; EnvValue += wcslen(EnvValue) + 1)
    {
        DPRINT1("Value: '%S'\n", EnvValue);
        // TODO: Convert ARC path to NT or something for partition list
    }
#endif

    return STATUS_SUCCESS;
}


/**
 * @brief
 * Finds system partitions on the system.
 *
 * On MBR disks, partition with the Active/Boot flag set.
 * On GPT disks, partition with the PARTITION_SYSTEM_GUID.
 *
 * Note that there can be more than one marked system partition
 * on a given disk.
 **/
NTSTATUS
PtMarkSystemPartitions(
    _In_ PPARTLIST PartList)
{
    // if ((ArchType == ARCH_PcAT) || (ArchType == ARCH_NEC98x86))
    return PtBiosMarkSystemPartitions(PartList);
    // else if (ArchType == ARCH_Arc)
        // return PtArcMarkSystemPartitions(PartList);
    // else if (ArchType == ARCH_Efi)
        // return PtEfiMarkSystemPartitions(PartList);
    // else
        // return STATUS_NOT_SUPPORTED;
}

/* EOF */
