/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Partition list functions
 * COPYRIGHT:   Copyright 2003-2019 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2018-2019 Hermes Belusca-Maito
 *
 * This code interfaces with the NT5+ storage stack and uses
 * the new IOCTL_DISK_**_EX disk IOCTLs where possible.
 */

#include "precomp.h"
#include <ntddscsi.h>

#include "partlist.h"
#include "fsrec.h"
#include "registry.h"

#define NDEBUG
#include <debug.h>

//#define DUMP_PARTITION_TABLE

#include <pshpack1.h>

typedef struct _REG_DISK_MOUNT_INFO
{
    ULONG Signature;
    LARGE_INTEGER StartingOffset;
} REG_DISK_MOUNT_INFO, *PREG_DISK_MOUNT_INFO;

#include <poppack.h>

/* The maximum information a DISK_GEOMETRY_EX dynamic structure can contain */
typedef struct _DISK_GEOMETRY_EX_INTERNAL
{
    DISK_GEOMETRY Geometry;
    LARGE_INTEGER DiskSize;
    DISK_PARTITION_INFO Partition;
    DISK_DETECTION_INFO Detection;
} DISK_GEOMETRY_EX_INTERNAL, *PDISK_GEOMETRY_EX_INTERNAL;

static PCSTR PartitionStyleNames[] = {"MBR", "GPT", "RAW", "Unknown"};
#define PARTITION_STYLE_NAME(PartStyle) \
    ( ((PartStyle) <= PARTITION_STYLE_RAW)   \
          ? PartitionStyleNames[(PartStyle)] \
          : PartitionStyleNames[RTL_NUMBER_OF(PartitionStyleNames)-1] )

#define DRIVE_LAYOUT_INFOEX_ENTRY_SIZE \
    RTL_FIELD_SIZE(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[0])

#define DRIVE_LAYOUT_INFOEX_SIZE(n) \
    (FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + \
        ((n) * DRIVE_LAYOUT_INFOEX_ENTRY_SIZE))

/* Rounding up for number; not the same as ROUND_UP() with power-of-two sizes */
#define ROUND_UP_NUM(num, up)   ((((num) + (up) - 1) / (up)) * (up))

/* Provides the maximum number of partition entries that can be
 * described by any given partition table in the disk. */
// TODO: For NEC98: Use IsNEC_98 ?
#define MAX_PARTITION_ENTRIES_LAYOUT(LayoutBuffer) \
    ( ((LayoutBuffer)->PartitionStyle == PARTITION_STYLE_MBR) ? 4 : \
      ((LayoutBuffer)->PartitionStyle == PARTITION_STYLE_GPT) ? (LayoutBuffer)->Gpt.MaxPartitionCount : \
      ((LayoutBuffer)->PartitionStyle == PARTITION_STYLE_BRFR ) ? 5 : \
      ((LayoutBuffer)->PartitionStyle == PARTITION_STYLE_NEC98) ? 16 : \
      ((LayoutBuffer)->PartitionStyle == PARTITION_STYLE_SUPERFLOPPY) ? 1 : \
      0 )

// TODO: For NEC98: Use IsNEC_98 ?
#define MAX_PARTITION_ENTRIES_DISKENTRY(DiskEntry) \
    ( ((DiskEntry)->DiskStyle == PARTITION_STYLE_MBR) ? 4 : \
      ((DiskEntry)->DiskStyle == PARTITION_STYLE_GPT) ? (DiskEntry)->LayoutBuffer->Gpt.MaxPartitionCount : \
      ((DiskEntry)->DiskStyle == PARTITION_STYLE_BRFR ) ? 5 : \
      ((DiskEntry)->DiskStyle == PARTITION_STYLE_NEC98) ? 16 : \
      ((DiskEntry)->DiskStyle == PARTITION_STYLE_SUPERFLOPPY) ? 1 : \
      0 )


/* FUNCTIONS ****************************************************************/

#define GUID_FORMAT_STR "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define GUID_ELEMENTS(Guid) \
    (Guid)->Data1, (Guid)->Data2, (Guid)->Data3, \
    (Guid)->Data4[0], (Guid)->Data4[1], (Guid)->Data4[2], (Guid)->Data4[3], \
    (Guid)->Data4[4], (Guid)->Data4[5], (Guid)->Data4[6], (Guid)->Data4[7]

#ifdef DUMP_PARTITION_TABLE
static
VOID
DumpPartitionTable(
    PDISKENTRY DiskEntry)
{
    PPARTITION_INFORMATION_EX PartitionInfo;
    ULONG i;

    DbgPrint("\n"
             "Disk Style: %s (%d)\n"
             "Index  Start         Length        Hidden      Nr  Type  Boot  RW\n"
             "-----  ------------  ------------  ----------  --  ----  ----  --\n",
             "n/a",
             DiskEntry->DiskStyle);

    for (i = 0; i < DiskEntry->LayoutBuffer->PartitionCount; i++)
    {
        PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[i];
        ASSERT(PartitionInfo->PartitionStyle == DiskEntry->DiskStyle);

// TODO: Different display depending on MBR or GPT.
//
        DbgPrint("  %3lu  %12I64u  %12I64u  %10lu  %2lu    %2x     %c   %c\n",
                 i,
                 PartitionInfo->StartingOffset.QuadPart / DiskEntry->BytesPerSector,
                 PartitionInfo->PartitionLength.QuadPart / DiskEntry->BytesPerSector,
//               PartitionInfo->HiddenSectors,
                 PartitionInfo->PartitionNumber,
//               PartitionInfo->PartitionType,
//               PartitionInfo->BootIndicator ? '*': ' ',
                 PartitionInfo->RewritePartition ? 'Y': 'N');
    }

    DbgPrint("\n");
}
#endif


ULONGLONG
AlignDown(
    IN ULONGLONG Value,
    IN ULONG Alignment)
{
    ULONGLONG Temp;

    Temp = Value / Alignment;

    return Temp * Alignment;
}

ULONGLONG
AlignUp(
    IN ULONGLONG Value,
    IN ULONG Alignment)
{
    ULONGLONG Temp, Result;

    Temp = Value / Alignment;

    Result = Temp * Alignment;
    if (Value % Alignment)
        Result += Alignment;

    return Result;
}

ULONGLONG
RoundingDivide(
   IN ULONGLONG Dividend,
   IN ULONGLONG Divisor)
{
    return (Dividend + Divisor / 2) / Divisor;
}


static
VOID
GetDriverName(
    IN PDISKENTRY DiskEntry)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    WCHAR KeyName[32];
    NTSTATUS Status;

    RtlInitUnicodeString(&DiskEntry->DriverName, NULL);

    RtlStringCchPrintfW(KeyName, ARRAYSIZE(KeyName),
                        L"\\Scsi\\Scsi Port %hu",
                        DiskEntry->Port);

    RtlZeroMemory(&QueryTable, sizeof(QueryTable));

    QueryTable[0].Name = L"Driver";
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].EntryContext = &DiskEntry->DriverName;

    /* This will allocate DiskEntry->DriverName if needed */
    Status = RtlQueryRegistryValues(RTL_REGISTRY_DEVICEMAP,
                                    KeyName,
                                    QueryTable,
                                    NULL,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlQueryRegistryValues() failed (Status %lx)\n", Status);
    }
}

/*
 * FIXME: Rely on the MOUNTMGR to assign the drive letters.
 *
 * For the moment, we do it ourselves, by assigning drives to partitions
 * that are *only on MBR disks*. We first assign letters to each active
 * partition on each disk, then assign letters to each logical partition,
 * and finish by assigning letters to the remaining primary partitions.
 * (This algorithm is the one that can be observed in the Windows installer.)
 */
static
VOID
AssignDriveLetters(
    IN PPARTLIST List)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    PLIST_ENTRY Entry1;
    PLIST_ENTRY Entry2;
    UINT i;
    WCHAR Letter;

    /* Start at letter 'C' ('A' and 'B' are reserved for floppy disks)
     * on PC-AT compatibles, except on NEC PC-98 where we start at 'A'. */
    Letter = IsNEC_98 ? L'A' : L'C';

    /* First, clean up all drive letters */
    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        /* Ignore disks that are not MBR */
        if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
            continue;

        for (i = PRIMARY_PARTITIONS;
             i <= (/*(DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
                      ?*/ LOGICAL_PARTITIONS /*: PRIMARY_PARTITIONS*/);
             ++i)
        {
            for (Entry2 =  DiskEntry->PartList[i].Flink;
                 Entry2 != &DiskEntry->PartList[i];
                 Entry2 =  Entry2->Flink)
            {
                PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);
                PartEntry->DriveLetter = 0;
            }
        }
    }

    /* Assign drive letters to active (boot) partitions */
    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        /* Ignore disks that are not MBR */
        if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
            continue;

        for (Entry2 = DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
             Entry2 != &DiskEntry->PartList[PRIMARY_PARTITIONS];
             Entry2 = Entry2->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);

            /* Check if the partition is partitioned, used and active */
            // if (IsSystemPartition(PartEntry))
            if (PartEntry->IsPartitioned &&
                // !IsContainerPartition(PartEntry->PartitionType.MbrType) &&
                PartEntry->IsSystemPartition)
            {
                /* Yes it is */
                ASSERT(PartEntry->PartitionType.MbrType != PARTITION_ENTRY_UNUSED);
                ASSERT(!IsContainerPartition(PartEntry->PartitionType.MbrType));

                // NOTE: Windows installer does not check for a recognized partition.
                if (IsRecognizedPartition(PartEntry->PartitionType.MbrType) ||
                    PartEntry->SectorCount.QuadPart != 0LL)
                {
                    if (Letter <= L'Z')
                    {
                        PartEntry->DriveLetter = Letter;
                        Letter++;
                    }
                }
            }
        }
    }

    /* Assign drive letters to logical partitions */
    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        /* Ignore disks that are not MBR */
        if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
            continue;

        for (Entry2 = DiskEntry->PartList[LOGICAL_PARTITIONS].Flink;
             Entry2 != &DiskEntry->PartList[LOGICAL_PARTITIONS];
             Entry2 = Entry2->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);

            if (PartEntry->IsPartitioned /**/ && PartEntry->DriveLetter == 0/**/)
            {
                ASSERT(PartEntry->PartitionType.MbrType != PARTITION_ENTRY_UNUSED);

                // NOTE: Windows installer does not check for a recognized partition.
                if (IsRecognizedPartition(PartEntry->PartitionType.MbrType) ||
                    PartEntry->SectorCount.QuadPart != 0LL)
                {
                    if (Letter <= L'Z')
                    {
                        PartEntry->DriveLetter = Letter;
                        Letter++;
                    }
                }
            }
        }
    }

    /* Assign drive letters to the remaining primary partitions */
    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        /* Ignore disks that are not MBR */
        if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
            continue;

        for (Entry2 = DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
             Entry2 != &DiskEntry->PartList[PRIMARY_PARTITIONS];
             Entry2 = Entry2->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);

            if (PartEntry->IsPartitioned &&
                !IsContainerPartition(PartEntry->PartitionType.MbrType)
                /**/ && PartEntry->DriveLetter == 0/**/)
            {
                ASSERT(PartEntry->PartitionType.MbrType != PARTITION_ENTRY_UNUSED);

                // NOTE: Windows installer does not check for a recognized partition.
                if (IsRecognizedPartition(PartEntry->PartitionType.MbrType) ||
                    PartEntry->SectorCount.QuadPart != 0LL)
                {
                    if (Letter <= L'Z')
                    {
                        PartEntry->DriveLetter = Letter;
                        Letter++;
                    }
                }
            }
        }
    }
}

static NTSTATUS
NTAPI
DiskIdentifierQueryRoutine(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
    PBIOSDISKENTRY BiosDiskEntry = (PBIOSDISKENTRY)Context;
    UNICODE_STRING NameU;

    if (ValueType == REG_SZ &&
        ValueLength == 20 * sizeof(WCHAR) &&
        ((PWCHAR)ValueData)[8] == L'-')
    {
        NameU.Buffer = (PWCHAR)ValueData;
        NameU.Length = NameU.MaximumLength = 8 * sizeof(WCHAR);
        RtlUnicodeStringToInteger(&NameU, 16, &BiosDiskEntry->Checksum);

        NameU.Buffer = (PWCHAR)ValueData + 9;
        RtlUnicodeStringToInteger(&NameU, 16, &BiosDiskEntry->Signature);

        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

#ifdef LEGACY_BIOS_DATA

static NTSTATUS
NTAPI
DiskConfigurationDataQueryRoutine(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
    PBIOSDISKENTRY BiosDiskEntry = (PBIOSDISKENTRY)Context;
    PCM_FULL_RESOURCE_DESCRIPTOR FullResourceDescriptor;
    PCM_DISK_GEOMETRY_DEVICE_DATA DiskGeometry;
    ULONG i;

    if (ValueType != REG_FULL_RESOURCE_DESCRIPTOR ||
        ValueLength < sizeof(CM_FULL_RESOURCE_DESCRIPTOR))
        return STATUS_UNSUCCESSFUL;

    FullResourceDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)ValueData;

    /* Hm. Version and Revision are not set on Microsoft Windows XP... */
#if 0
    if (FullResourceDescriptor->PartialResourceList.Version != 1 ||
        FullResourceDescriptor->PartialResourceList.Revision != 1)
        return STATUS_UNSUCCESSFUL;
#endif

    for (i = 0; i < FullResourceDescriptor->PartialResourceList.Count; i++)
    {
        if (FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].Type != CmResourceTypeDeviceSpecific ||
            FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize != sizeof(CM_DISK_GEOMETRY_DEVICE_DATA))
            continue;

        DiskGeometry = (PCM_DISK_GEOMETRY_DEVICE_DATA)&FullResourceDescriptor->PartialResourceList.PartialDescriptors[i + 1];
        BiosDiskEntry->DiskGeometry = *DiskGeometry;

        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

static NTSTATUS
NTAPI
SystemConfigurationDataQueryRoutine(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullResourceDescriptor;
    PCM_INT13_DRIVE_PARAMETER* Int13Drives = (PCM_INT13_DRIVE_PARAMETER*)Context;
    ULONG i;

    if (ValueType != REG_FULL_RESOURCE_DESCRIPTOR ||
        ValueLength < sizeof(CM_FULL_RESOURCE_DESCRIPTOR))
        return STATUS_UNSUCCESSFUL;

    FullResourceDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)ValueData;

    /* Hm. Version and Revision are not set on Microsoft Windows XP... */
#if 0
    if (FullResourceDescriptor->PartialResourceList.Version != 1 ||
        FullResourceDescriptor->PartialResourceList.Revision != 1)
        return STATUS_UNSUCCESSFUL;
#endif

    for (i = 0; i < FullResourceDescriptor->PartialResourceList.Count; i++)
    {
        if (FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].Type != CmResourceTypeDeviceSpecific ||
            FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize % sizeof(CM_INT13_DRIVE_PARAMETER) != 0)
            continue;

        *Int13Drives = (CM_INT13_DRIVE_PARAMETER*)RtlAllocateHeap(ProcessHeap, 0,
                       FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize);
        if (*Int13Drives == NULL)
            return STATUS_NO_MEMORY;

        memcpy(*Int13Drives,
               &FullResourceDescriptor->PartialResourceList.PartialDescriptors[i + 1],
               FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize);
        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

#endif // LEGACY_BIOS_DATA

static VOID
EnumerateBiosDiskEntries(
    IN PPARTLIST PartList)
{
#ifdef LEGACY_BIOS_DATA
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
#else
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
#endif
    WCHAR Name[120];
    ULONG AdapterCount;
    ULONG ControllerCount;
    ULONG DiskCount;
    NTSTATUS Status;
#ifdef LEGACY_BIOS_DATA
    PCM_INT13_DRIVE_PARAMETER Int13Drives;
#endif
    PBIOSDISKENTRY BiosDiskEntry;

#define ROOT_NAME   L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\MultifunctionAdapter"

    RtlZeroMemory(QueryTable, sizeof(QueryTable));

#ifdef LEGACY_BIOS_DATA
    QueryTable[1].Name = L"Configuration Data";
    QueryTable[1].QueryRoutine = SystemConfigurationDataQueryRoutine;
    Int13Drives = NULL;
    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                    L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System",
                                    &QueryTable[1],
                                    (PVOID)&Int13Drives,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Unable to query the 'Configuration Data' key in '\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System', status=%lx\n", Status);
        return;
    }
#endif // LEGACY_BIOS_DATA

    for (AdapterCount = 0; ; ++AdapterCount)
    {
        RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                            L"%s\\%lu",
                            ROOT_NAME, AdapterCount);
        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                        Name,
#ifdef LEGACY_BIOS_DATA
                                        &QueryTable[2],
#else
                                        &QueryTable[1],
#endif
                                        NULL,
                                        NULL);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                            L"%s\\%lu\\DiskController",
                            ROOT_NAME, AdapterCount);
        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                        Name,
#ifdef LEGACY_BIOS_DATA
                                        &QueryTable[2],
#else
                                        &QueryTable[1],
#endif
                                        NULL,
                                        NULL);
        if (NT_SUCCESS(Status))
        {
            for (ControllerCount = 0; ; ++ControllerCount)
            {
                RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                                    L"%s\\%lu\\DiskController\\%lu",
                                    ROOT_NAME, AdapterCount, ControllerCount);
                Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                                Name,
#ifdef LEGACY_BIOS_DATA
                                                &QueryTable[2],
#else
                                                &QueryTable[1],
#endif
                                                NULL,
                                                NULL);
                if (!NT_SUCCESS(Status))
                {
#ifdef LEGACY_BIOS_DATA
                    RtlFreeHeap(ProcessHeap, 0, Int13Drives);
#endif
                    return;
                }

                RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                                    L"%s\\%lu\\DiskController\\%lu\\DiskPeripheral",
                                    ROOT_NAME, AdapterCount, ControllerCount);
                Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                                Name,
#ifdef LEGACY_BIOS_DATA
                                                &QueryTable[2],
#else
                                                &QueryTable[1],
#endif
                                                NULL,
                                                NULL);
                if (NT_SUCCESS(Status))
                {
                    QueryTable[0].Name = L"Identifier";
                    QueryTable[0].QueryRoutine = DiskIdentifierQueryRoutine;
#ifdef LEGACY_BIOS_DATA
                    QueryTable[1].Name = L"Configuration Data";
                    QueryTable[1].QueryRoutine = DiskConfigurationDataQueryRoutine;
#endif

                    for (DiskCount = 0; ; ++DiskCount)
                    {
                        BiosDiskEntry = (BIOSDISKENTRY*)RtlAllocateHeap(ProcessHeap, HEAP_ZERO_MEMORY, sizeof(BIOSDISKENTRY));
                        if (BiosDiskEntry == NULL)
                        {
#ifdef LEGACY_BIOS_DATA
                            RtlFreeHeap(ProcessHeap, 0, Int13Drives);
#endif
                            return;
                        }

                        RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                                            L"%s\\%lu\\DiskController\\%lu\\DiskPeripheral\\%lu",
                                            ROOT_NAME, AdapterCount, ControllerCount, DiskCount);
                        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                                        Name,
                                                        QueryTable,
                                                        (PVOID)BiosDiskEntry,
                                                        NULL);
                        if (!NT_SUCCESS(Status))
                        {
                            RtlFreeHeap(ProcessHeap, 0, BiosDiskEntry);
#ifdef LEGACY_BIOS_DATA
                            RtlFreeHeap(ProcessHeap, 0, Int13Drives);
#endif
                            return;
                        }

                        BiosDiskEntry->AdapterNumber = 0; // And NOT "AdapterCount" as it needs to be hardcoded for BIOS!
                        BiosDiskEntry->ControllerNumber = ControllerCount;
                        BiosDiskEntry->DiskNumber = DiskCount;
                        BiosDiskEntry->DiskEntry = NULL;

#ifdef LEGACY_BIOS_DATA
                        if (DiskCount < Int13Drives[0].NumberDrives)
                        {
                            BiosDiskEntry->Int13DiskData = Int13Drives[DiskCount];
                        }
                        else
                        {
                            DPRINT1("Didn't find Int13 drive data for disk %u\n", DiskCount);
                        }
#endif // LEGACY_BIOS_DATA

                        InsertTailList(&PartList->BiosDiskListHead, &BiosDiskEntry->ListEntry);

#undef DPRINT
#define DPRINT DPRINT1
                        DPRINT("--->\n");
                        DPRINT("AdapterNumber:     %lu\n", BiosDiskEntry->AdapterNumber);
                        DPRINT("ControllerNumber:  %lu\n", BiosDiskEntry->ControllerNumber);
                        DPRINT("DiskNumber:        %lu\n", BiosDiskEntry->DiskNumber);
                        DPRINT("Signature:         %08lx\n", BiosDiskEntry->Signature);
                        DPRINT("Checksum:          %08lx\n", BiosDiskEntry->Checksum);
#ifdef LEGACY_BIOS_DATA
                        DPRINT("BytesPerSector:    %lu\n", BiosDiskEntry->DiskGeometry.BytesPerSector);
                        DPRINT("NumberOfCylinders: %lu\n", BiosDiskEntry->DiskGeometry.NumberOfCylinders);
                        DPRINT("NumberOfHeads:     %lu\n", BiosDiskEntry->DiskGeometry.NumberOfHeads);
                        DPRINT("DriveSelect:       %02x\n", BiosDiskEntry->Int13DiskData.DriveSelect);
                        DPRINT("MaxCylinders:      %lu\n", BiosDiskEntry->Int13DiskData.MaxCylinders);
                        DPRINT("SectorsPerTrack:   %d\n", BiosDiskEntry->Int13DiskData.SectorsPerTrack);
                        DPRINT("MaxHeads:          %d\n", BiosDiskEntry->Int13DiskData.MaxHeads);
                        DPRINT("NumberDrives:      %d\n", BiosDiskEntry->Int13DiskData.NumberDrives);
#endif // LEGACY_BIOS_DATA
                        DPRINT("<---\n");
#undef DPRINT
#define DPRINT
                    }
                }
            }
        }
    }

#ifdef LEGACY_BIOS_DATA
    RtlFreeHeap(ProcessHeap, 0, Int13Drives);
#endif

#undef ROOT_NAME
}


NTSTATUS
OpenDiskPartition(
    OUT PHANDLE PartitionHandle,    // IN OUT PHANDLE OPTIONAL
    IN HANDLE DiskHandle OPTIONAL,
    IN ULONG DiskNumber OPTIONAL,
    IN ULONG PartitionNumber,
    IN BOOLEAN ReadWriteAccess,
    IN BOOLEAN ReadWriteShare)
{
    NTSTATUS Status;
    UNICODE_STRING FileName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR PathBuffer[MAX_PATH];

    /* Open the disk and partition */

    if (DiskHandle == NULL)
    {
        RtlStringCchPrintfW(PathBuffer, ARRAYSIZE(PathBuffer),
                            L"\\Device\\Harddisk%lu\\Partition%lu",
                            DiskNumber,
                            PartitionNumber);
    }
    else
    {
        RtlStringCchPrintfW(PathBuffer, ARRAYSIZE(PathBuffer),
                            L"Partition%lu",
                            PartitionNumber);
    }

    RtlInitUnicodeString(&FileName, PathBuffer);
    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE,
                               DiskHandle,
                               NULL);

    *PartitionHandle = NULL;
    Status = NtOpenFile(PartitionHandle,
                        // FILE_READ_DATA | /* FILE_READ_ATTRIBUTES | */ SYNCHRONIZE
                        FILE_GENERIC_READ | // Contains SYNCHRONIZE
                            (ReadWriteAccess ? FILE_GENERIC_WRITE : 0),
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ |
                            (ReadWriteShare ? FILE_SHARE_WRITE : 0),
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        if (DiskHandle == NULL)
        {
            DPRINT1("Failed to open partition %lu in disk %lu, Status 0x%08lx\n",
                    PartitionNumber, DiskNumber, Status);
        }
        else
        {
            DPRINT1("Failed to open partition %lu in disk 0x%p, Status 0x%08lx\n",
                    PartitionNumber, DiskHandle, Status);
        }
        return Status;
    }

    return STATUS_SUCCESS;
}


/*
 * Detects whether a disk reports as a "super-floppy", i.e. an unpartitioned
 * disk with a valid VBR, following the criteria used by IoReadPartitionTable()
 * and IoWritePartitionTable():
 * only one single partition starting at the beginning of the disk; the reported
 * defaults are: partition number being zero and its type being FAT16 non-bootable.
 * Note also that accessing \Device\HarddiskN\Partition0 or Partition1 returns
 * the same data.
 */
static
BOOLEAN
IsSuperFloppy(
    IN PDISKENTRY DiskEntry)
{
    PPARTITION_INFORMATION_EX PartitionInfo;
    ULONGLONG PartitionLengthEstimate;

    /* No layout buffer: we cannot say anything yet */
    if (DiskEntry->LayoutBuffer == NULL)
        return FALSE;

    /* If the disk has already been checked as such, return success */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_SUPERFLOPPY)
        return TRUE;

    /* Only MBR-reported type disks can be super-floppy */
    ASSERT(DiskEntry->DiskStyle == DiskEntry->LayoutBuffer->PartitionStyle);
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
        return FALSE;

    /* We must have only one partition */
    if (DiskEntry->LayoutBuffer->PartitionCount != 1)
        return FALSE;

    /* Get the single partition entry */
    PartitionInfo = DiskEntry->LayoutBuffer->PartitionEntry;
    ASSERT(PartitionInfo->PartitionStyle == DiskEntry->LayoutBuffer->PartitionStyle);

    /* The single partition must start at the beginning of the disk */
    if (!(PartitionInfo->StartingOffset.QuadPart == 0 &&
          PartitionInfo->Mbr.HiddenSectors == 0))
    {
        return FALSE;
    }

    /* The disk signature is usually set to one; warn in case it's not */
    if (DiskEntry->LayoutBuffer->Signature != 1)
    {
        DPRINT1("Super-Floppy disk %lu signature %08x != 1!\n",
                DiskEntry->DiskNumber, DiskEntry->LayoutBuffer->Signature);
    }

    /*
     * The partition number must be zero or one, be recognized,
     * have FAT16 type and report as non-bootable.
     */
    if ((PartitionInfo->PartitionNumber != 0 &&
         PartitionInfo->PartitionNumber != 1) ||
        PartitionInfo->Mbr.RecognizedPartition != TRUE ||
        PartitionInfo->Mbr.PartitionType != PARTITION_FAT_16 ||
        PartitionInfo->Mbr.BootIndicator != FALSE)
    {
        DPRINT1("Super-Floppy disk %lu does not return default settings!\n"
                "    PartitionNumber = %lu, expected 0\n"
                "    RecognizedPartition = %s, expected TRUE\n"
                "    PartitionType = 0x%02x, expected 0x04 (PARTITION_FAT_16)\n"
                "    BootIndicator = %s, expected FALSE\n",
                DiskEntry->DiskNumber,
                PartitionInfo->PartitionNumber,
                PartitionInfo->Mbr.RecognizedPartition ? "TRUE" : "FALSE",
                PartitionInfo->Mbr.PartitionType,
                PartitionInfo->Mbr.BootIndicator ? "TRUE" : "FALSE");
    }

    /* The partition lengths should agree */
    PartitionLengthEstimate = DiskEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector;
    if (PartitionInfo->PartitionLength.QuadPart != PartitionLengthEstimate)
    {
        DPRINT1("PartitionLength = %I64u is different from PartitionLengthEstimate = %I64u\n",
                PartitionInfo->PartitionLength.QuadPart, PartitionLengthEstimate);
    }

    return TRUE;
}



// Detect if disk is "dynamic": Find the presence of any partition
// with ID == PARTITION_LDM / GUID == PARTITION_LDM_DATA_GUID .

#if 0
static BOOLEAN
UninitializeDisk(
    IN PDISKENTRY DiskEntry)
{
    NTSTATUS Status;
    PPARTENTRY PartEntry;
    PLIST_ENTRY Entry;
    UINT i;
    HANDLE FileHandle;
    IO_STATUS_BLOCK Iosb;
    CREATE_DISK CreateInfo;

    /* Do nothing if the disk is already uninitialized */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_RAW)
        return TRUE;

    // TODO: Dismount all volumes on this disk.

    /* Free the partition lists */
    for (i = PRIMARY_PARTITIONS; i <= LOGICAL_PARTITIONS; ++i)
    {
        while (!IsListEmpty(&DiskEntry->PartList[i]))
        {
            Entry = RemoveHeadList(&DiskEntry->PartList[i]);
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
            RtlFreeHeap(ProcessHeap, 0, PartEntry);
        }
    }

    /* Free the layout buffer */
    if (DiskEntry->LayoutBuffer != NULL)
        RtlFreeHeap(ProcessHeap, 0, DiskEntry->LayoutBuffer);
    DiskEntry->LayoutBuffer = NULL;


    // TODO: Patch MBR/GPT to uninitialize the disk, or call
    // IOCTL_DISK_CREATE_DISK with PARTITION_STYLE_RAW
    // which does the job.

    DiskEntry->DiskStyle = PARTITION_STYLE_RAW;

    Status = OpenDiskPartition(&FileHandle,
                               NULL,
                               DiskEntry->DiskNumber, 0,
                               TRUE, FALSE); // Actually, no share flags whatsoever.
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Could not open disk %lu for reset, Status 0x%08lx\n",
                DiskEntry->DiskNumber, Status);
        return FALSE;
    }

    CreateInfo.PartitionStyle = DiskEntry->DiskStyle;
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_DISK_CREATE_DISK,
                                   CreateInfo,
                                   sizeof(CreateInfo),
                                   NULL,
                                   0);
    /* Use a legacy call in case "invalid function" has been emitted */
    if (Status == STATUS_INVALID_DEVICE_REQUEST)
    {
        Status = NtDeviceIoControlFile(FileHandle,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Iosb,
                                       IOCTL_DISK_DELETE_DRIVE_LAYOUT,
                                       NULL,
                                       0,
                                       NULL,
                                       0);
    }

    NtClose(FileHandle);

    return TRUE;
}
#endif


static ULONG KsecRandomSeed = 0x62b409a1;

// A private function, based on KeQueryTickCount(), that would be nice if
// MS ever created it. The Rtl/NtGetTickCount() instead packs everything
// in a ULONG, thus losing precision.
VOID
NTAPI
NtQueryTickCount(
    OUT PLARGE_INTEGER TickCount)
{
#ifdef _WIN64
    TickCount->QuadPart = *((volatile ULONG64*)&SharedUserData->TickCount);
#else
    /* Loop until we get a perfect match */
    for (;;)
    {
        /* Read the tick count value */
        TickCount->HighPart = SharedUserData->TickCount.High1Time;
        TickCount->LowPart = SharedUserData->TickCount.LowPart;
        if (TickCount->HighPart == SharedUserData->TickCount.High2Time)
            break;
        YieldProcessor();
    }
#endif
}

// RtlGenRandom
/* Implementation copied from ksecdd!KsecGenRandom() */
BOOLEAN
NTAPI
SystemFunction036(
    OUT PVOID Buffer,
    IN /*SIZE_T*/ ULONG Length)
{
    LARGE_INTEGER TickCount;
    ULONG i, RandomValue;
    PULONG P;

    /* Try to generate a more random seed */
    RtlQueryTickCount(&TickCount);
    KsecRandomSeed ^= _rotl(TickCount.LowPart, (KsecRandomSeed % 23));

    P = Buffer;
    for (i = 0; i < Length / sizeof(ULONG); i++)
    {
        P[i] = RtlRandomEx(&KsecRandomSeed);
    }
    Length &= (sizeof(ULONG) - 1);
    if (Length > 0)
    {
        RandomValue = RtlRandomEx(&KsecRandomSeed);
        RtlCopyMemory(&P[i], &RandomValue, Length);
    }

    return TRUE;
}


#if 0

VOID
DiskGenMbrSignature(
    OUT PULONG Signature)
{
    LARGE_INTEGER SystemTime;
    TIME_FIELDS TimeFields;
    PUCHAR Buffer = (PUCHAR)Signature;

    NtQuerySystemTime(&SystemTime);
    RtlTimeToTimeFields(&SystemTime, &TimeFields);

    Buffer[0] = (UCHAR)(TimeFields.Year & 0xFF) + (UCHAR)(TimeFields.Hour & 0xFF);
    Buffer[1] = (UCHAR)(TimeFields.Year >> 8) + (UCHAR)(TimeFields.Minute & 0xFF);
    Buffer[2] = (UCHAR)(TimeFields.Month & 0xFF) + (UCHAR)(TimeFields.Second & 0xFF);
    Buffer[3] = (UCHAR)(TimeFields.Day & 0xFF) + (UCHAR)(TimeFields.Milliseconds & 0xFF);
}

#else

#define DiskGenMbrSignature(Signature) \
    (*(Signature) = RtlRandomEx(&KsecRandomSeed))

#endif

// #define DiskGenGptSignature(Guid) \
    UuidCreate((UUID*)Guid)

/* Implementation similar to rpcrt4!UuidCreate() */
VOID
DiskGenGptSignature(
    OUT GUID* Guid)
{
    if (!Guid)
        return;

    RtlGenRandom(Guid, sizeof(*Guid));
    /* Clear the version bits and set the version (4) */
    Guid->Data3 &= 0x0fff;
    Guid->Data3 |= (4 << 12);
    /* Set the topmost bits of Data4 (clock_seq_hi_and_reserved) as
     * specified in RFC 4122, section 4.4. */
    Guid->Data4[0] &= 0x3f;
    Guid->Data4[0] |= 0x80;
}

NTSTATUS
InitializeDisk(
    IN PDISKENTRY DiskEntry,
    IN PARTITION_STYLE DiskStyle,
    IN PCREATE_DISK DiskInfo OPTIONAL)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;
    CREATE_DISK CreateInfo;

    /* Fail if the disk is already initialized */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_RAW)
        return STATUS_PARTITION_FAILURE;

    switch (DiskStyle)
    {
    case PARTITION_STYLE_MBR:
        CreateInfo.PartitionStyle = PARTITION_STYLE_MBR;
        DiskGenMbrSignature(&CreateInfo.Mbr.Signature);
        break;

    case PARTITION_STYLE_GPT:
        CreateInfo.PartitionStyle = PARTITION_STYLE_GPT;
        DiskGenGptSignature(&CreateInfo.Gpt.DiskId);
        // The underlying code will adjust to the actual minimum value (128).
        CreateInfo.Gpt.MaxPartitionCount = 0;
        break;

    case PARTITION_STYLE_SUPERFLOPPY:
        UNIMPLEMENTED;
        return STATUS_NOT_IMPLEMENTED;

    // case PARTITION_STYLE_BRFR:
    //    break;

    default:
        break;
    }

    Status = OpenDiskPartition(&FileHandle,
                               NULL,
                               DiskEntry->DiskNumber, 0,
                               TRUE, FALSE); // Actually, no share flags whatsoever.
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Could not open disk %lu for reset, Status 0x%08lx\n",
                DiskEntry->DiskNumber, Status);
        return Status;
    }

    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_DISK_CREATE_DISK,
                                   CreateInfo,
                                   sizeof(CreateInfo),
                                   NULL,
                                   0);
    if (NT_SUCCESS(Status))
        DiskEntry->DiskStyle = DiskStyle;

    NtClose(FileHandle);

    return Status;
}


/*
 * Inserts the disk region represented by PartEntry into the adequate
 * partition list of a given disk.
 * The lists are kept sorted by increasing order of start sectors.
 * Of course no disk region should overlap at all with one another.
 */
static
BOOLEAN
InsertDiskRegion(
    IN OUT PLIST_ENTRY ListHead,
    IN PPARTENTRY PartEntry)
{
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry2;

    /* Find the first disk region before which we need to insert the new one */
    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PartEntry2 = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

        /* Ignore any unused empty region */
        if ((IS_PARTITION_UNUSED(PartEntry2) &&
             PartEntry2->StartSector.QuadPart == 0) || PartEntry2->SectorCount.QuadPart == 0)
        {
            continue;
        }

        /* If the current region ends before the one to be inserted, try again */
        if (PartEntry2->StartSector.QuadPart + PartEntry2->SectorCount.QuadPart - 1
                < PartEntry->StartSector.QuadPart)
        {
            continue;
        }

        /*
         * One of the disk region boundaries crosses the desired region
         * (it starts after the desired region, or ends before the end
         * of the desired region): this is an impossible situation because
         * disk regions (partitions) cannot overlap!
         * Throw an error and bail out.
         */
        if (max(PartEntry->StartSector.QuadPart, PartEntry2->StartSector.QuadPart)
            <=
            min( PartEntry->StartSector.QuadPart +  PartEntry->SectorCount.QuadPart - 1,
                PartEntry2->StartSector.QuadPart + PartEntry2->SectorCount.QuadPart - 1))
        {
            DPRINT1("Disk region overlap problem, stopping there!\n"
                    "Partition to be inserted:\n"
                    "    StartSector = %I64u ; EndSector = %I64u\n"
                    "Existing disk region:\n"
                    "    StartSector = %I64u ; EndSector = %I64u\n",
                     PartEntry->StartSector.QuadPart,
                     PartEntry->StartSector.QuadPart +  PartEntry->SectorCount.QuadPart - 1,
                    PartEntry2->StartSector.QuadPart,
                    PartEntry2->StartSector.QuadPart + PartEntry2->SectorCount.QuadPart - 1);
            return FALSE;
        }

        /* We have found the first region before which the new one has to be inserted */
        break;
    }

    /* Insert the disk region */
    InsertTailList(Entry, &PartEntry->ListEntry);
    return TRUE;
}

static
PPARTENTRY
CreateInsertBlankRegion(
    IN PDISKENTRY DiskEntry,
    IN OUT PLIST_ENTRY ListHead,
    IN ULONGLONG StartSector,
    IN ULONGLONG SectorCount,
    IN BOOLEAN LogicalSpace)
{
    PPARTENTRY NewPartEntry;

    NewPartEntry = RtlAllocateHeap(ProcessHeap,
                                   HEAP_ZERO_MEMORY,
                                   sizeof(PARTENTRY));
    if (NewPartEntry == NULL)
        return NULL;

    NewPartEntry->DiskEntry = DiskEntry;

    NewPartEntry->StartSector.QuadPart = StartSector;
    NewPartEntry->SectorCount.QuadPart = SectorCount;

    NewPartEntry->LogicalPartition = LogicalSpace;
    NewPartEntry->IsPartitioned = FALSE;
    RtlZeroMemory(&NewPartEntry->PartitionType, sizeof(NewPartEntry->PartitionType));
    NewPartEntry->FormatState = Unformatted;
    NewPartEntry->FileSystem[0] = L'\0';

    DPRINT1("First Sector : %I64u\n", NewPartEntry->StartSector.QuadPart);
    DPRINT1("Last Sector  : %I64u\n", NewPartEntry->StartSector.QuadPart + NewPartEntry->SectorCount.QuadPart - 1);
    DPRINT1("Total Sectors: %I64u\n", NewPartEntry->SectorCount.QuadPart);

    /* Insert the new entry into the list */
    InsertTailList(ListHead, &NewPartEntry->ListEntry);

    return NewPartEntry;
}

static
BOOLEAN
InitializePartitionEntry(
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount,
    IN BOOLEAN AutoCreate)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    DPRINT1("Current partition sector count: %I64u\n", PartEntry->SectorCount.QuadPart);

    /* Fail if we try to initialize this partition entry with more sectors than what it actually contains */
    if (SectorCount > PartEntry->SectorCount.QuadPart)
        return FALSE;

    /* Fail if the partition is already in use */
    ASSERT(!PartEntry->IsPartitioned);

    if ((AutoCreate != FALSE) ||
        (AlignDown(PartEntry->StartSector.QuadPart + SectorCount, DiskEntry->SectorAlignment) -
                   PartEntry->StartSector.QuadPart == PartEntry->SectorCount.QuadPart))
    {
        PartEntry->AutoCreate = AutoCreate;
    }
    else
    {
        ULONGLONG StartSector;
        ULONGLONG SectorCount2;
        PPARTENTRY NewPartEntry;

        /* Create a partition entry that represents the remaining space after the partition to be initialized */

        StartSector = AlignDown(PartEntry->StartSector.QuadPart + SectorCount, DiskEntry->SectorAlignment);
        SectorCount2 = PartEntry->StartSector.QuadPart + PartEntry->SectorCount.QuadPart - StartSector;

        NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                               PartEntry->ListEntry.Flink,
                                               StartSector,
                                               SectorCount2,
                                               PartEntry->LogicalPartition);
        if (NewPartEntry == NULL)
        {
            DPRINT1("Failed to create a new empty region for disk space!\n");
            return FALSE;
        }

        /* Resize down the partition entry; its StartSector remains the same */
        PartEntry->SectorCount.QuadPart = StartSector - PartEntry->StartSector.QuadPart;
    }

    /* Convert the partition entry to 'New (Unformatted)' */
    PartEntry->New = TRUE;
    PartEntry->IsPartitioned = TRUE;

    RtlZeroMemory(&PartEntry->PartInfo, sizeof(PartEntry->PartInfo));
    if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
    {
        PartEntry->PartitionType.MbrType =
            FileSystemToMBRPartitionType(L"RAW",
                                         PartEntry->StartSector.QuadPart,
                                         PartEntry->SectorCount.QuadPart);
        ASSERT(PartEntry->PartitionType.MbrType != PARTITION_ENTRY_UNUSED);
    }
    else if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        PartEntry->PartitionType.GptType = PARTITION_BASIC_DATA_GUID;
    }

    PartEntry->FormatState = Unformatted;
    PartEntry->FileSystem[0] = L'\0';
    // PartEntry->AutoCreate = AutoCreate;
    PartEntry->IsSystemPartition = FALSE;

    DPRINT1("First Sector : %I64u\n", PartEntry->StartSector.QuadPart);
    DPRINT1("Last Sector  : %I64u\n", PartEntry->StartSector.QuadPart + PartEntry->SectorCount.QuadPart - 1);
    DPRINT1("Total Sectors: %I64u\n", PartEntry->SectorCount.QuadPart);

    return TRUE;
}


static
VOID
AddPartitionToDisk(
    IN PDISKENTRY DiskEntry,
    IN ULONG PartitionIndex,
    IN BOOLEAN LogicalPartition)
{
    NTSTATUS Status;
    PPARTITION_INFORMATION_EX PartitionInfo;
    PPARTENTRY PartEntry;
    HANDLE PartitionHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    UCHAR LabelBuffer[sizeof(FILE_FS_VOLUME_INFORMATION) + 256 * sizeof(WCHAR)];
    PFILE_FS_VOLUME_INFORMATION LabelInfo = (PFILE_FS_VOLUME_INFORMATION)LabelBuffer;

    PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[PartitionIndex];

    if (PartitionInfo->PartitionType == PARTITION_ENTRY_UNUSED ||
        ((LogicalPartition != FALSE) && IsContainerPartition(PartitionInfo->PartitionType)))
    {
        return;
    }

    PartEntry = RtlAllocateHeap(ProcessHeap,
                                HEAP_ZERO_MEMORY,
                                sizeof(PARTENTRY));
    if (PartEntry == NULL)
        return;

    PartEntry->DiskEntry = DiskEntry;

    PartEntry->StartSector.QuadPart = (ULONGLONG)PartitionInfo->StartingOffset.QuadPart / DiskEntry->BytesPerSector;
    PartEntry->SectorCount.QuadPart = (ULONGLONG)PartitionInfo->PartitionLength.QuadPart / DiskEntry->BytesPerSector;

    PartEntry->IsSystemPartition =
        (DiskEntry->DiskStyle == PARTITION_STYLE_MBR ? PartitionInfo->Mbr.BootIndicator :
         DiskEntry->DiskStyle == PARTITION_STYLE_GPT ? IsEqualPartitionType(PartitionInfo->Gpt.PartitionType,
                                                                            PARTITION_SYSTEM_GUID) : FALSE);

    RtlZeroMemory(&PartEntry->PartitionType, sizeof(PartEntry->PartitionType));
    RtlZeroMemory(&PartEntry->PartInfo, sizeof(PartEntry->PartInfo));
    if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
    {
        PartEntry->PartitionType.MbrType = PartitionInfo->Mbr.PartitionType;
        PartEntry->PartInfo.Mbr = PartitionInfo->Mbr;
    }
    else if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        PartEntry->PartitionType.GptType = PartitionInfo->Gpt.PartitionType;
        PartEntry->PartInfo.Gpt = PartitionInfo->Gpt;
    }

    PartEntry->LogicalPartition = LogicalPartition;
    PartEntry->IsPartitioned = TRUE;
    PartEntry->OnDiskPartitionNumber = PartitionInfo->PartitionNumber;
    PartEntry->PartitionNumber = PartitionInfo->PartitionNumber;
    PartEntry->PartitionIndex = PartitionIndex;

    /* Specify the partition as initially unformatted */
    PartEntry->FormatState = Unformatted;
    PartEntry->FileSystem[0] = L'\0';

    /* Initialize the partition volume label */
    RtlZeroMemory(PartEntry->VolumeLabel, sizeof(PartEntry->VolumeLabel));

    if ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) &&
        IsContainerPartition(PartEntry->PartitionType.MbrType))
    {
        PartEntry->FormatState = Unformatted;

        if (LogicalPartition == FALSE && DiskEntry->ExtendedPartition == NULL)
            DiskEntry->ExtendedPartition = PartEntry;
    }
    else if (IS_RECOGNIZED_PARTITION(DiskEntry->DiskStyle,
                                     PartEntry->PartitionType))
    {
        ASSERT(PartitionInfo->RecognizedPartition);
        ASSERT(PartEntry->IsPartitioned && PartEntry->PartitionNumber != 0);

        /* Try to open the volume so as to mount it */
        Status = OpenDiskPartition(&PartitionHandle,
                                   NULL,
                                   DiskEntry->DiskNumber,
                                   PartEntry->PartitionNumber,
                                   FALSE, TRUE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("OpenDiskPartition() failed, Status 0x%08lx\n", Status);
        }

        if (PartitionHandle)
        {
            ASSERT(NT_SUCCESS(Status));

            /* We don't have a FS, try to guess one */
            Status = InferFileSystem(NULL, PartitionHandle,
                                     PartEntry->FileSystem,
                                     sizeof(PartEntry->FileSystem));
            if (!NT_SUCCESS(Status))
                DPRINT1("InferFileSystem() failed, Status 0x%08lx\n", Status);
        }
        if (*PartEntry->FileSystem)
        {
            ASSERT(PartitionHandle);

            /*
             * Handle partition mounted with RawFS: it is
             * either unformatted or has an unknown format.
             */
            if (wcsicmp(PartEntry->FileSystem, L"RAW") == 0)
            {
                /*
                 * For MBR disks only:
                 * True unformatted partitions on NT are created with their
                 * partition type set to either one of the following values,
                 * and are mounted with RawFS. This is done this way since we
                 * are assured to have FAT support, which is the only FS that
                 * uses these partition types. Therefore, having a partition
                 * mounted with RawFS and with these partition types means that
                 * the FAT FS was unable to mount it beforehand and thus the
                 * partition is unformatted.
                 * However, any partition mounted by RawFS that does NOT have
                 * any of these partition types must be considered as having
                 * an unknown format.
                 */
                if ( ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) &&
                      (PartEntry->PartitionType.MbrType == PARTITION_FAT_12 ||
                       PartEntry->PartitionType.MbrType == PARTITION_FAT_16 ||
                       PartEntry->PartitionType.MbrType == PARTITION_HUGE   ||
                       PartEntry->PartitionType.MbrType == PARTITION_XINT13 ||
                       PartEntry->PartitionType.MbrType == PARTITION_FAT32  ||
                       PartEntry->PartitionType.MbrType == PARTITION_FAT32_XINT13)
                      ) ||
                     ((DiskEntry->DiskStyle == PARTITION_STYLE_GPT) &&
                      IsEqualPartitionType(PartEntry->PartitionType.GptType,
                                           PARTITION_BASIC_DATA_GUID)) )
                {
                    PartEntry->FormatState = Unformatted;
                }
                else
                {
                    /* Close the partition before dismounting */
                    NtClose(PartitionHandle);
                    PartitionHandle = NULL;
                    /*
                     * Dismount the partition since RawFS owns it, and set its
                     * format to unknown (may or may not be actually formatted).
                     */
                    DismountVolume(PartEntry);
                    PartEntry->FormatState = UnknownFormat;
                    PartEntry->FileSystem[0] = L'\0';
                }
            }
            else
            {
                PartEntry->FormatState = Preformatted;
            }
        }
        else
        {
            PartEntry->FormatState = UnknownFormat;
        }

        /* Retrieve the partition volume label */
        if (PartitionHandle)
        {
            Status = NtQueryVolumeInformationFile(PartitionHandle,
                                                  &IoStatusBlock,
                                                  &LabelBuffer,
                                                  sizeof(LabelBuffer),
                                                  FileFsVolumeInformation);
            if (NT_SUCCESS(Status))
            {
                /* Copy the (possibly truncated) volume label and NULL-terminate it */
                RtlStringCbCopyNW(PartEntry->VolumeLabel, sizeof(PartEntry->VolumeLabel),
                                  LabelInfo->VolumeLabel, LabelInfo->VolumeLabelLength);
            }
            else
            {
                DPRINT1("NtQueryVolumeInformationFile() failed, Status 0x%08lx\n", Status);
            }
        }

        /* Close the partition */
        if (PartitionHandle)
            NtClose(PartitionHandle);
    }
    else
    {
        /* Unknown partition, hence unknown format (may or may not be actually formatted) */
        PartEntry->FormatState = UnknownFormat;
    }

    /* Use the correct partition list */
    InsertDiskRegion(&DiskEntry->PartList[LogicalPartition
                                            ? LOGICAL_PARTITIONS
                                            : PRIMARY_PARTITIONS],
                     PartEntry);
}

static
VOID
AddLogicalDiskSpace(
    IN PDISKENTRY DiskEntry)
{
    ULONGLONG StartSector;
    ULONGLONG SectorCount;
    PPARTENTRY NewPartEntry;

    /* Create a partition entry that represents the empty space in the container partition */

    StartSector = DiskEntry->ExtendedPartition->StartSector.QuadPart + (ULONGLONG)DiskEntry->SectorAlignment;
    SectorCount = DiskEntry->ExtendedPartition->SectorCount.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment;

    NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                           &DiskEntry->PartList[LOGICAL_PARTITIONS],
                                           StartSector,
                                           SectorCount,
                                           TRUE);
    if (NewPartEntry == NULL)
        DPRINT1("Failed to create a new empty region for full extended partition space!\n");
}

static
VOID
ScanForUnpartitionedDiskSpace(
    IN PDISKENTRY DiskEntry)
{
    ULONGLONG StartSector;
    ULONGLONG SectorCount;
    ULONGLONG LastStartSector;
    ULONGLONG LastSectorCount;
    ULONGLONG LastUnusedSectorCount;
    PPARTENTRY PartEntry;
    PPARTENTRY NewPartEntry;
    PLIST_ENTRY Entry;

    DPRINT("ScanForUnpartitionedDiskSpace()\n");

    if (IsListEmpty(&DiskEntry->PartList[PRIMARY_PARTITIONS]))
    {
        DPRINT1("No primary partition!\n");

        /* Create a partition entry that represents the empty disk */

        if (DiskEntry->SectorAlignment < 2048)
            StartSector = 2048ULL;
        else
            StartSector = (ULONGLONG)DiskEntry->SectorAlignment;
        SectorCount = AlignDown(DiskEntry->SectorCount.QuadPart, DiskEntry->SectorAlignment) - StartSector;

        NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                               &DiskEntry->PartList[PRIMARY_PARTITIONS],
                                               StartSector,
                                               SectorCount,
                                               FALSE);
        if (NewPartEntry == NULL)
            DPRINT1("Failed to create a new empty region for full disk space!\n");

        return;
    }

    /* Start partition at head 1, cylinder 0 */
    if (DiskEntry->SectorAlignment < 2048)
        LastStartSector = 2048ULL;
    else
        LastStartSector = (ULONGLONG)DiskEntry->SectorAlignment;
    LastSectorCount = 0ULL;
    LastUnusedSectorCount = 0ULL;

    for (Entry = DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
         Entry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
         Entry = Entry->Flink)
    {
        PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

        if (!IS_PARTITION_UNUSED(PartEntry) ||
            PartEntry->SectorCount.QuadPart != 0ULL)
        {
            LastUnusedSectorCount =
                PartEntry->StartSector.QuadPart - (LastStartSector + LastSectorCount);

            if (PartEntry->StartSector.QuadPart > (LastStartSector + LastSectorCount) &&
                LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
            {
                DPRINT("Unpartitioned disk space %I64u sectors\n", LastUnusedSectorCount);

                StartSector = LastStartSector + LastSectorCount;
                SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

                /* Insert the table into the list */
                NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                       &PartEntry->ListEntry,
                                                       StartSector,
                                                       SectorCount,
                                                       FALSE);
                if (NewPartEntry == NULL)
                {
                    DPRINT1("Failed to create a new empty region for disk space!\n");
                    return;
                }
            }

            LastStartSector = PartEntry->StartSector.QuadPart;
            LastSectorCount = PartEntry->SectorCount.QuadPart;
        }
    }

    /* Check for trailing unpartitioned disk space */
    if ((LastStartSector + LastSectorCount) < DiskEntry->SectorCount.QuadPart)
    {
        LastUnusedSectorCount = AlignDown(DiskEntry->SectorCount.QuadPart - (LastStartSector + LastSectorCount), DiskEntry->SectorAlignment);

        if (LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
        {
            DPRINT("Unpartitioned disk space: %I64u sectors\n", LastUnusedSectorCount);

            StartSector = LastStartSector + LastSectorCount;
            SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

            /* Append the table to the list */
            NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                   &DiskEntry->PartList[PRIMARY_PARTITIONS],
                                                   StartSector,
                                                   SectorCount,
                                                   FALSE);
            if (NewPartEntry == NULL)
            {
                DPRINT1("Failed to create a new empty region for trailing disk space!\n");
                return;
            }
        }
    }

    /* Ignore logical partitions for disks that are not MBR */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
        return NULL;

    if (DiskEntry->ExtendedPartition != NULL)
    {
        if (IsListEmpty(&DiskEntry->PartList[LOGICAL_PARTITIONS]))
        {
            DPRINT1("No logical partition!\n");

            /* Create a partition entry that represents the empty space in the container partition */
            AddLogicalDiskSpace(DiskEntry);
            return;
        }

        /* Start partition at head 1, cylinder 0 */
        LastStartSector = DiskEntry->ExtendedPartition->StartSector.QuadPart + (ULONGLONG)DiskEntry->SectorAlignment;
        LastSectorCount = 0ULL;
        LastUnusedSectorCount = 0ULL;

        for (Entry = DiskEntry->PartList[LOGICAL_PARTITIONS].Flink;
             Entry != &DiskEntry->PartList[LOGICAL_PARTITIONS];
             Entry = Entry->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            if (!IS_PARTITION_UNUSED(PartEntry) ||
                PartEntry->SectorCount.QuadPart != 0ULL)
            {
                LastUnusedSectorCount =
                    PartEntry->StartSector.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment - (LastStartSector + LastSectorCount);

                if ((PartEntry->StartSector.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment) > (LastStartSector + LastSectorCount) &&
                    LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
                {
                    DPRINT("Unpartitioned disk space %I64u sectors\n", LastUnusedSectorCount);

                    StartSector = LastStartSector + LastSectorCount;
                    SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

                    /* Insert the table into the list */
                    NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                           &PartEntry->ListEntry,
                                                           StartSector,
                                                           SectorCount,
                                                           TRUE);
                    if (NewPartEntry == NULL)
                    {
                        DPRINT1("Failed to create a new empty region for extended partition space!\n");
                        return;
                    }
                }

                LastStartSector = PartEntry->StartSector.QuadPart;
                LastSectorCount = PartEntry->SectorCount.QuadPart;
            }
        }

        /* Check for trailing unpartitioned disk space */
        if ((LastStartSector + LastSectorCount) < DiskEntry->ExtendedPartition->StartSector.QuadPart + DiskEntry->ExtendedPartition->SectorCount.QuadPart)
        {
            LastUnusedSectorCount = AlignDown(DiskEntry->ExtendedPartition->StartSector.QuadPart +
                                              DiskEntry->ExtendedPartition->SectorCount.QuadPart - (LastStartSector + LastSectorCount),
                                              DiskEntry->SectorAlignment);

            if (LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
            {
                DPRINT("Unpartitioned disk space: %I64u sectors\n", LastUnusedSectorCount);

                StartSector = LastStartSector + LastSectorCount;
                SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

                /* Append the table to the list */
                NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                       &DiskEntry->PartList[LOGICAL_PARTITIONS],
                                                       StartSector,
                                                       SectorCount,
                                                       TRUE);
                if (NewPartEntry == NULL)
                {
                    DPRINT1("Failed to create a new empty region for extended partition space!\n");
                    return;
                }
            }
        }
    }

    DPRINT("ScanForUnpartitionedDiskSpace() done\n");
}

static
VOID
SetDiskSignature(
    IN PPARTLIST List,
    IN PDISKENTRY DiskEntry)
{
    PLIST_ENTRY Entry2;
    PDISKENTRY DiskEntry2;

    ASSERT(DiskEntry->LayoutBuffer);
    ASSERT(DiskEntry->DiskStyle == DiskEntry->LayoutBuffer->PartitionStyle);

    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT("Disk of style %d does not support signatures.\n",
               DiskEntry->DiskStyle);
        return;
    }

    /* Regenerate the disk signature as long as we have collisions with other disks */
    while (TRUE)
    {
        if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
        {
            DiskGenMbrSignature(&DiskEntry->LayoutBuffer->Mbr.Signature);
            if (DiskEntry->LayoutBuffer->Mbr.Signature == 0)
                continue;
        }
        else // if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        {
            DiskGenGptSignature(&DiskEntry->LayoutBuffer->Gpt.DiskId);
            if (IsEqualGUID(&DiskEntry->LayoutBuffer->Gpt.DiskId, &GUID_NULL))
                continue;
        }

        /* Check if the signature already exist */
        /* FIXME:
         *   Check also signatures from disks, which are
         *   not visible (bootable) by the bios.
         */
        for (Entry2 = List->DiskListHead.Flink;
             Entry2 != &List->DiskListHead;
             Entry2 = Entry2->Flink)
        {
            DiskEntry2 = CONTAINING_RECORD(Entry2, DISKENTRY, ListEntry);

            /* Check that the disks are different, but have the same type */
            if (!(DiskEntry != DiskEntry2 &&
                  DiskEntry->DiskStyle == DiskEntry2->DiskStyle))
            {
                continue;
            }

            /* Compare their signatures */
            if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
            {
                if (DiskEntry->LayoutBuffer->Mbr.Signature ==
                    DiskEntry2->LayoutBuffer->Mbr.Signature)
                {
                    break;
                }
            }
            else // if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
            {
                if (IsEqualGUID(&DiskEntry->LayoutBuffer->Gpt.DiskId,
                                &DiskEntry2->LayoutBuffer->Gpt.DiskId))
                {
                    break;
                }
            }
        }
        /* No need to retry if we enumerated all the disks */
        if (Entry2 == &List->DiskListHead)
            break;
    }
}

static
VOID
UpdateDiskSignatures(
    IN PPARTLIST List)
{
    PLIST_ENTRY Entry;
    PDISKENTRY DiskEntry;

    /* Update each already-initialized disk */
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        if (DiskEntry->PartitionStyle != PARTITION_STYLE_RAW &&
            DiskEntry->LayoutBuffer &&
            DiskEntry->LayoutBuffer->Signature == 0)
        {
            SetDiskSignature(List, DiskEntry);
            DiskEntry->LayoutBuffer->PartitionEntry[0].RewritePartition = TRUE;
        }
    }
}

static
VOID
UpdateHwDiskNumbers(
    IN PPARTLIST List)
{
    PLIST_ENTRY ListEntry;
    PBIOSDISKENTRY BiosDiskEntry;
    PDISKENTRY DiskEntry;
    ULONG HwAdapterNumber = 0;
    ULONG HwControllerNumber = 0;
    ULONG RemovableDiskCount = 0;

    /*
     * Enumerate the disks recognized by the BIOS and recompute the disk
     * numbers on the system when *ALL* removable disks are not connected.
     * The entries are inserted in increasing order of AdapterNumber,
     * ControllerNumber and DiskNumber.
     */
    for (ListEntry = List->BiosDiskListHead.Flink;
         ListEntry != &List->BiosDiskListHead;
         ListEntry = ListEntry->Flink)
    {
        BiosDiskEntry = CONTAINING_RECORD(ListEntry, BIOSDISKENTRY, ListEntry);
        DiskEntry = BiosDiskEntry->DiskEntry;

        /*
         * If the adapter or controller numbers change, update them and reset
         * the number of removable disks on this adapter/controller.
         */
        if (HwAdapterNumber != BiosDiskEntry->AdapterNumber ||
            HwControllerNumber != BiosDiskEntry->ControllerNumber)
        {
            HwAdapterNumber = BiosDiskEntry->AdapterNumber;
            HwControllerNumber = BiosDiskEntry->ControllerNumber;
            RemovableDiskCount = 0;
        }

        /* Adjust the actual hardware disk number */
        if (DiskEntry)
        {
            ASSERT(DiskEntry->HwDiskNumber == BiosDiskEntry->DiskNumber);

            if (DiskEntry->MediaType == RemovableMedia)
            {
                /* Increase the number of removable disks and set the disk number to zero */
                ++RemovableDiskCount;
                DiskEntry->HwFixedDiskNumber = 0;
            }
            else // if (DiskEntry->MediaType == FixedMedia)
            {
                /* Adjust the fixed disk number, offset by the number of removable disks found before this one */
                DiskEntry->HwFixedDiskNumber = BiosDiskEntry->DiskNumber - RemovableDiskCount;
            }
        }
        else
        {
            DPRINT1("BIOS disk %lu is not recognized by NTOS!\n", BiosDiskEntry->DiskNumber);
        }
    }
}

static
VOID
AddDiskToList(
    IN HANDLE FileHandle,
    IN ULONG DiskNumber,
    IN PPARTLIST List)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;
    DISK_GEOMETRY_EX_INTERNAL DiskInfo;
    SCSI_ADDRESS ScsiAddress;
    PDISKENTRY DiskEntry;

    union
    {
        PPARTITION_SECTOR Mbr;
        PULONG Buffer;
    } DiskSector0;

    LARGE_INTEGER FileOffset;
    WCHAR Identifier[20];
    ULONG Checksum;
    ULONG Signature;
    ULONG i;
    PLIST_ENTRY ListEntry;
    PBIOSDISKENTRY BiosDiskEntry;
    ULONG LayoutBufferSize;
    PDRIVE_LAYOUT_INFORMATION_EX NewLayoutBuffer;
    PPARTITION_INFORMATION_EX PartitionInfo;

    /* Retrieve the disk geometry, partition and detection information */
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                   NULL,
                                   0,
                                   &DiskInfo,
                                   sizeof(DiskInfo));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtDeviceIoControlFile(IOCTL_DISK_GET_DRIVE_GEOMETRY_EX) failed (Status: 0x%08lx)\n", Status);
        return;
    }

    if (DiskInfo.Geometry.MediaType != FixedMedia &&
        DiskInfo.Geometry.MediaType != RemovableMedia)
    {
        DPRINT1("Disk %lu of unknown media type, ignoring it...\n", DiskNumber);
        return;
    }

    /*
     * FIXME: Here we suppose the disk is always SCSI. What if it is
     * of another type? To check this we need to retrieve the name of
     * the driver the disk device belongs to.
     */
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_SCSI_GET_ADDRESS,
                                   NULL,
                                   0,
                                   &ScsiAddress,
                                   sizeof(ScsiAddress));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtDeviceIoControlFile(IOCTL_SCSI_GET_ADDRESS) failed (Status: 0x%08lx)\n", Status);
        return;
    }

    /*
     * Read the first disk sector to retrieve the sector's signature
     * and checksum.
     *
     * Here we are interested only in the first disk sector, regardless
     * of the nature of the disk (MBR, GPT...) (in the GPT case we need
     * the info from its protective compatibility MBR, that is not returned
     * via the IOCTL_DISK_GET_DRIVE_GEOMETRY_EX call from above).
     */
    //
    // FIXME: What about NEC PC-98 or XBOX?
    //
    DiskSector0.Mbr =
        (PARTITION_SECTOR*)RtlAllocateHeap(ProcessHeap,
                                           0,
                                           DiskInfo.Geometry.BytesPerSector);
    if (DiskSector0.Mbr == NULL)
    {
        DPRINT1("Disk %lu of unknown media type, ignoring it...\n", DiskNumber);
        return;
    }

    FileOffset.QuadPart = 0;
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &Iosb,
                        (PVOID)DiskSector0.Mbr,
                        DiskInfo.Geometry.BytesPerSector,
                        &FileOffset,
                        NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, DiskSector0.Mbr);
        DPRINT1("NtReadFile() failed (Status: 0x%08lx)\n", Status);
        return;
    }

    Signature = DiskSector0.Mbr->Signature;

    /* Calculate the MBR checksum */
    Checksum = 0;
    // DiskSector0.Buffer = (PULONG)DiskSector0.Mbr;
    for (i = 0; i < 128; i++)
    {
        Checksum += DiskSector0.Buffer[i];
    }
    Checksum = ~Checksum + 1;

    /* If the disk was reported as of MBR type, compare our
     * obtained signature and checksum with the reported ones. */
    if (DiskInfo.Partition.PartitionStyle == PARTITION_STYLE_MBR)
    {
        if (DiskInfo.Partition.Mbr.Signature != Signature)
        {
            DPRINT1("Discrepancy between reported signature 0x%08x and calculated signature 0x%08x\n",
                    DiskInfo.Partition.Mbr.Signature, Signature);
        }
        if (DiskInfo.Partition.Mbr.CheckSum != Checksum)
        {
            DPRINT1("Discrepancy between reported checksum 0x%08x and calculated checksum 0x%08x\n",
                    DiskInfo.Partition.Mbr.CheckSum, Checksum);
        }
    }

    /* Build the disk identifier string */
    RtlStringCchPrintfW(Identifier, ARRAYSIZE(Identifier),
                        L"%08x-%08x-%c",
                        Checksum, Signature,
                        (DiskSector0.Mbr->Magic == PARTITION_MAGIC) ? L'A' : L'X');
    DPRINT("Identifier: %S\n", Identifier);

    /*
     * Check for any discrepancy in the disk type.
     * If we do not have the 0xAA55 then it's a RAW partition.
     */
    if ((DiskSector0.Mbr->Magic != PARTITION_MAGIC) &&
        (DiskInfo.Partition.PartitionStyle != PARTITION_STYLE_RAW))
    {
        DPRINT1("Overwriting disk partition style to RAW !!\n");
        DiskInfo.Partition.PartitionStyle = PARTITION_STYLE_RAW;
    }

    RtlFreeHeap(ProcessHeap, 0, DiskSector0.Mbr);

    DPRINT1("Disk %lu of identifier '%S' is %s\n",
            DiskNumber, Identifier,
            PARTITION_STYLE_NAME(DiskInfo.Partition.PartitionStyle));


    /* Allocate a new disk entry and make it reference the main list */
    DiskEntry = RtlAllocateHeap(ProcessHeap,
                                HEAP_ZERO_MEMORY,
                                sizeof(DISKENTRY));
    if (DiskEntry == NULL)
    {
        DPRINT1("Failed to allocate a new disk entry.\n");
        return;
    }

    DiskEntry->PartList = List;

    /* Retrieve the type of the disk */
    {
        FILE_FS_DEVICE_INFORMATION FileFsDevice;

        /* Query the device for its type */
        Status = NtQueryVolumeInformationFile(FileHandle,
                                              &Iosb,
                                              &FileFsDevice,
                                              sizeof(FileFsDevice),
                                              FileFsDeviceInformation);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Couldn't detect device type for disk %lu of identifier '%S'...\n", DiskNumber, Identifier);
        }
        else
        {
            DPRINT1("Disk %lu : DeviceType: 0x%08x ; Characteristics: 0x%08x\n", DiskNumber, FileFsDevice.DeviceType, FileFsDevice.Characteristics);
        }
    }
    // NOTE: We may also use NtQueryVolumeInformationFile(FileFsDeviceInformation).
    DiskEntry->MediaType = DiskInfo.Geometry.MediaType;
    if (DiskEntry->MediaType == RemovableMedia)
    {
        DPRINT1("Disk %lu of identifier '%S' is removable\n", DiskNumber, Identifier);
    }
    else // if (DiskEntry->MediaType == FixedMedia)
    {
        DPRINT1("Disk %lu of identifier '%S' is fixed\n", DiskNumber, Identifier);
    }

//    DiskEntry->Checksum = Checksum;
//    DiskEntry->Signature = Signature;
    DiskEntry->DiskStyle = DiskInfo.Partition.PartitionStyle;


    /*
     * Check whether the disk has been recognized by the underlying
     * boot firmware (is present in the list of FW recognized disks
     * previously retrieved).
     */
    DiskEntry->BiosFound = FALSE;

    for (ListEntry = List->BiosDiskListHead.Flink;
         ListEntry != &List->BiosDiskListHead;
         ListEntry = ListEntry->Flink)
    {
        BiosDiskEntry = CONTAINING_RECORD(ListEntry, BIOSDISKENTRY, ListEntry);
        /* FIXME:
         *   Compare the size from bios and the reported size from driver.
         *   If we have more than one disk with a zero or with the same signature
         *   we must create new signatures and reboot. After the reboot,
         *   it is possible to identify the disks.
         */
        if (BiosDiskEntry->Signature == Signature &&
            BiosDiskEntry->Checksum == Checksum &&
            BiosDiskEntry->DiskEntry == NULL)
        {
            if (!DiskEntry->BiosFound)
            {
                DiskEntry->HwAdapterNumber = BiosDiskEntry->AdapterNumber;
                DiskEntry->HwControllerNumber = BiosDiskEntry->ControllerNumber;
                DiskEntry->HwDiskNumber = BiosDiskEntry->DiskNumber;

                if (DiskEntry->MediaType == RemovableMedia)
                {
                    /* Set the removable disk number to zero */
                    DiskEntry->HwFixedDiskNumber = 0;
                }
                else // if (DiskEntry->MediaType == FixedMedia)
                {
                    /* The fixed disk number will later be adjusted using the number of removable disks */
                    DiskEntry->HwFixedDiskNumber = BiosDiskEntry->DiskNumber;
                }

                DiskEntry->BiosFound = TRUE;
                BiosDiskEntry->DiskEntry = DiskEntry;
                break;
            }
            else
            {
                // FIXME: What to do?
                DPRINT1("Disk %lu of identifier '%S' has already been found?!\n", DiskNumber, Identifier);
            }
        }
    }

    if (!DiskEntry->BiosFound)
    {
        DPRINT1("WARNING: Setup could not find a matching BIOS disk entry. Disk %lu may not be bootable by the BIOS!\n", DiskNumber);
    }

    DiskEntry->Cylinders = DiskInfo.Geometry.Cylinders.QuadPart;
    DiskEntry->TracksPerCylinder = DiskInfo.Geometry.TracksPerCylinder;
    DiskEntry->SectorsPerTrack = DiskInfo.Geometry.SectorsPerTrack;
    DiskEntry->BytesPerSector = DiskInfo.Geometry.BytesPerSector;

    DPRINT("Cylinders %I64u\n", DiskEntry->Cylinders);
    DPRINT("TracksPerCylinder %lu\n", DiskEntry->TracksPerCylinder);
    DPRINT("SectorsPerTrack %lu\n", DiskEntry->SectorsPerTrack);
    DPRINT("BytesPerSector %lu\n", DiskEntry->BytesPerSector);

    DiskEntry->SectorCount.QuadPart = DiskInfo.Geometry.Cylinders.QuadPart *
                                      (ULONGLONG)DiskInfo.Geometry.TracksPerCylinder *
                                      (ULONGLONG)DiskInfo.Geometry.SectorsPerTrack;

    DiskEntry->SectorAlignment = DiskInfo.Geometry.SectorsPerTrack;
    DiskEntry->CylinderAlignment = DiskInfo.Geometry.TracksPerCylinder *
                                   DiskInfo.Geometry.SectorsPerTrack;

    DPRINT("SectorCount %I64u\n", DiskEntry->SectorCount.QuadPart);
    DPRINT("SectorAlignment %lu\n", DiskEntry->SectorAlignment);

    DiskEntry->DiskNumber = DiskNumber;
    DiskEntry->Port = ScsiAddress.PortNumber;
    DiskEntry->Bus = ScsiAddress.PathId;
    DiskEntry->Id = ScsiAddress.TargetId;

    GetDriverName(DiskEntry);
    /*
     * Actually it would be more correct somehow to use:
     *
     * OBJECT_NAME_INFORMATION NameInfo; // ObjectNameInfo;
     * ULONG ReturnedLength;
     *
     * Status = NtQueryObject(SomeHandleToTheDisk,
     *                        ObjectNameInformation,
     *                        &NameInfo,
     *                        sizeof(NameInfo),
     *                        &ReturnedLength);
     * etc...
     *
     * See examples in https://git.reactos.org/?p=reactos.git;a=blob;f=reactos/ntoskrnl/io/iomgr/error.c;hb=2f3a93ee9cec8322a86bf74b356f1ad83fc912dc#l267
     */

    InitializeListHead(&DiskEntry->PartList[PRIMARY_PARTITIONS]);
    InitializeListHead(&DiskEntry->PartList[LOGICAL_PARTITIONS]);

    InsertAscendingList(&List->DiskListHead, DiskEntry, DISKENTRY, ListEntry, DiskNumber);


    /* Stop there now if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("Disk of style %d is not MBR or GPT and is not supported!\n",
                DiskEntry->DiskStyle);
        return;
    }


    /*
     * We now retrieve the disk partition layout
     */

    /* Allocate a layout buffer with initially 4 partition entries (or 16 for NEC PC-98) */
    LayoutBufferSize = DRIVE_LAYOUT_INFOEX_SIZE(IsNEC_98 ? 16 : 4);
    DiskEntry->LayoutBuffer = RtlAllocateHeap(ProcessHeap,
                                              HEAP_ZERO_MEMORY,
                                              LayoutBufferSize);
    if (DiskEntry->LayoutBuffer == NULL)
    {
        DPRINT1("Failed to allocate the disk layout buffer!\n");
        return;
    }

    /*
     * Keep looping while the drive layout buffer is too small.
     * Iosb.Information or PartitionCount only contain actual info only
     * once NtDeviceIoControlFile(IOCTL_DISK_GET_DRIVE_LAYOUT_EX) succeeds.
     */
    for (;;)
    {
        Status = NtDeviceIoControlFile(FileHandle,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Iosb,
                                       IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                       NULL,
                                       0,
                                       DiskEntry->LayoutBuffer,
                                       LayoutBufferSize);
        if (NT_SUCCESS(Status))
        {
            /* We succeeded; compactify the layout structure but keep the
             * number of partition entries rounded up to a multiple of 4. */
#if 1
            LayoutBufferSize = DRIVE_LAYOUT_INFOEX_SIZE(ROUND_UP_NUM(DiskEntry->LayoutBuffer->PartitionCount, 4));
#else
            LayoutBufferSize = (ULONG)Iosb.Information;
#endif
            NewLayoutBuffer = RtlReAllocateHeap(ProcessHeap,
                                                HEAP_REALLOC_IN_PLACE_ONLY,
                                                DiskEntry->LayoutBuffer,
                                                LayoutBufferSize);
            if (!NewLayoutBuffer)
            {
                DPRINT1("Compactification failed; keeping original structure.\n");
            }
            else
            {
                DiskEntry->LayoutBuffer = NewLayoutBuffer;
            }
            Status = STATUS_SUCCESS;
            break;
        }

        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            DPRINT1("NtDeviceIoControlFile(IOCTL_DISK_GET_DRIVE_LAYOUT_EX) failed (Status: 0x%08lx)\n", Status);

            // /* Bail out if any other error than "invalid function" has been emitted */
            // if (Status != STATUS_INVALID_DEVICE_REQUEST) ...
            // TODO: Otherwise fall back to legacy IOCTL_DISK_GET_DRIVE_LAYOUT
            // and convert the layout back to extended.

            RtlFreeHeap(RtlGetProcessHeap(), 0, DiskEntry->LayoutBuffer);
            DiskEntry->LayoutBuffer = NULL;
            return;
        }

        /* Reallocate the buffer by chunks of 4 entries */
        LayoutBufferSize += 4 * DRIVE_LAYOUT_INFOEX_ENTRY_SIZE;
        NewLayoutBuffer = RtlReAllocateHeap(ProcessHeap,
                                            HEAP_ZERO_MEMORY,
                                            DiskEntry->LayoutBuffer,
                                            LayoutBufferSize);
        if (NewLayoutBuffer == NULL)
        {
            DPRINT1("Failed to reallocate the disk layout buffer!\n");
            RtlFreeHeap(RtlGetProcessHeap(), 0, DiskEntry->LayoutBuffer);
            DiskEntry->LayoutBuffer = NULL;
            return;
        }
        DiskEntry->LayoutBuffer = NewLayoutBuffer;
    }

    DPRINT1("PartitionCount: %lu\n", DiskEntry->LayoutBuffer->PartitionCount);

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif

    if (IsSuperFloppy(DiskEntry))
    {
        DPRINT1("Disk %lu is a super-floppy\n", DiskNumber);
        DiskEntry->DiskStyle = PARTITION_STYLE_SUPERFLOPPY;
    }

    /* Inform about default sector alignment being used */
    PartitionInfo = DiskEntry->LayoutBuffer->PartitionEntry[0];
    if (PartitionInfo->StartingOffset.QuadPart != 0 &&
        PartitionInfo->PartitionLength.QuadPart != 0 &&
        PartitionInfo->PartitionType != PARTITION_ENTRY_UNUSED)
    {
        if ((PartitionInfo->StartingOffset.QuadPart / DiskEntry->BytesPerSector) % DiskEntry->SectorsPerTrack == 0)
        {
            DPRINT("Use %lu Sector alignment!\n", DiskEntry->SectorsPerTrack);
        }
        else if (PartitionInfo->StartingOffset.QuadPart % (1024 * 1024) == 0)
        {
            DPRINT1("Use megabyte (%lu Sectors) alignment!\n",
                    (1024 * 1024) / DiskEntry->BytesPerSector);
        }
        else
        {
            DPRINT1("No matching alignment found! Partition 1 starts at %I64u\n",
                    PartitionInfo->StartingOffset.QuadPart);
        }
    }
    else
    {
        DPRINT1("No valid partition table found! Use megabyte (%lu Sectors) alignment!\n",
                (1024 * 1024) / DiskEntry->BytesPerSector);
    }

    /*
     * Add the partitions into the lists.
     */
    if (DiskEntry->LayoutBuffer->PartitionCount == 0)
    {
        DiskEntry->NewDisk = TRUE;

        /* Initialize a partition count to a default
         * non-zero value only if the disk is MBR. */
        if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
        {
            DiskEntry->LayoutBuffer->PartitionCount = (IsNEC_98 ? 16 : 4);

            /* Invalidate these entries as well */
            for (i = 0; i < DiskEntry->LayoutBuffer->PartitionCount; ++i)
            {
                DiskEntry->LayoutBuffer->PartitionEntry[i].RewritePartition = TRUE;
            }
        }
    }
    else
    {
        if ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) && !IsNEC_98)
        {
            /* Enumerate and add the first four primary partitions */
            for (i = 0; i < 4; ++i)
            {
                AddPartitionToDisk(DiskEntry, i, FALSE);
            }

            /* Enumerate and add the remaining partitions as logical ones */
            for (i = 4; i < DiskEntry->LayoutBuffer->PartitionCount; i += 4)
            {
                AddPartitionToDisk(DiskEntry, i, TRUE);
            }
        }
        else // if ( ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) && IsNEC_98) ||
             //       (DiskEntry->DiskStyle == PARTITION_STYLE_GPT) )
        {
            /* Enumerate and add the partitions (all primary) */
            for (i = 0; i < DiskEntry->LayoutBuffer->PartitionCount; ++i)
            {
                AddPartitionToDisk(DiskEntry, i, FALSE);
            }
        }
    }

    ScanForUnpartitionedDiskSpace(DiskEntry);
}

/*
 * Retrieve the system disk, i.e. the fixed disk that is accessible by the
 * firmware during boot time and where the system partition resides.
 * If no system partition has been determined, we retrieve the first disk
 * that verifies the mentioned criteria above.
 */
static
PDISKENTRY
GetSystemDisk(
    IN PPARTLIST List)
{
    PLIST_ENTRY Entry;
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
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        /* The disk must be a fixed disk and be found by the firmware */
        if (DiskEntry->MediaType == FixedMedia && DiskEntry->BiosFound)
        {
            break;
        }
    }
    if (Entry == &List->DiskListHead)
    {
        /* We haven't encountered any suitable disk */
        return NULL;
    }

    /* Display warning if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("System disk of style %d is not supported!\n",
                DiskEntry->DiskStyle);
        return NULL;
    }

    return DiskEntry;
}

/*
 * Retrieve the actual "active" partition of the given disk.
 * On MBR disks, partition with the Active/Boot flag set;
 * on GPT disks, partition with the correct GUID.
 */
BOOLEAN
IsSystemPartition(
    IN PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    ASSERT(DiskEntry);

    /* Fail if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("Disk of style %d is not MBR or GPT and does not support partitions.\n",
                DiskEntry->DiskStyle);
        return FALSE;
    }

    if ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) &&
        IsContainerPartition(PartEntry->PartitionType.MbrType))
    {
        return FALSE;
    }

    /* Check if the partition is partitioned, used and active */
    if (PartEntry->IsPartitioned && PartEntry->IsSystemPartition)
    {
        /* Yes it is */
        ASSERT(!IS_PARTITION_UNUSED(PartEntry));
        return TRUE;
    }

    return FALSE;
}

static
PPARTENTRY
GetActiveDiskPartition(
    IN PDISKENTRY DiskEntry)
{
    PLIST_ENTRY ListEntry;
    PPARTENTRY PartEntry;
    PPARTENTRY ActivePartition = NULL;

    /* Check for empty disk list */
    // ASSERT(DiskEntry);
    if (!DiskEntry)
        return NULL;

    /* Check for empty partition list */
    if (IsListEmpty(&DiskEntry->PartList[PRIMARY_PARTITIONS]))
        return NULL;

    /* Fail if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("Disk of style %d is not MBR or GPT and does not support partitions.\n",
                DiskEntry->DiskStyle);
        return NULL;
    }

    /* Scan all (primary) partitions to find the active disk partition */
    for (ListEntry = DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
         ListEntry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
         ListEntry = ListEntry->Flink)
    {
        /* Retrieve the partition */
        PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);
        if (IsSystemPartition(PartEntry))
        {
            /* Yes, we've found it */
            ASSERT(DiskEntry == PartEntry->DiskEntry);
            ASSERT(PartEntry->IsPartitioned);

            ActivePartition = PartEntry;

            DPRINT1("Found active system partition %lu in disk %lu, drive letter %C\n",
                    PartEntry->PartitionNumber, DiskEntry->DiskNumber,
                    (PartEntry->DriveLetter == 0) ? L'-' : PartEntry->DriveLetter);
            break;
        }
    }

    /* Check if the disk is new and if so, use its first partition as the active system partition */
    if (DiskEntry->NewDisk && ActivePartition != NULL)
    {
        // FIXME: What to do??
        DPRINT1("NewDisk TRUE but already existing active partition?\n");
    }

    /* Return the active partition found (or none) */
    return ActivePartition;
}

PPARTLIST
CreatePartitionList(VOID)
{
    PPARTLIST List;
    PDISKENTRY SystemDisk;
    NTSTATUS Status;
    SYSTEM_DEVICE_INFORMATION Sdi;
    ULONG ReturnSize;
    ULONG DiskNumber;
    HANDLE FileHandle;

    List = (PPARTLIST)RtlAllocateHeap(ProcessHeap,
                                      0,
                                      sizeof(PARTLIST));
    if (List == NULL)
        return NULL;

    List->SystemPartition = NULL;

    InitializeListHead(&List->DiskListHead);
    InitializeListHead(&List->BiosDiskListHead);

    /*
     * Enumerate the disks seen by the BIOS; this will be used later
     * to map drives seen by NTOS with their corresponding BIOS names.
     */
    EnumerateBiosDiskEntries(List);

    /* Enumerate disks seen by NTOS */
    Status = NtQuerySystemInformation(SystemDeviceInformation,
                                      &Sdi,
                                      sizeof(Sdi),
                                      &ReturnSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtQuerySystemInformation() failed, Status 0x%08lx", Status);
        RtlFreeHeap(ProcessHeap, 0, List);
        return NULL;
    }

    for (DiskNumber = 0; DiskNumber < Sdi.NumberOfDisks; DiskNumber++)
    {
        Status = OpenDiskPartition(&FileHandle,
                                   NULL,
                                   DiskNumber, 0,
                                   FALSE, TRUE);
                                   // FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE
        if (NT_SUCCESS(Status))
        {
            AddDiskToList(FileHandle, DiskNumber, List);
            NtClose(FileHandle);
        }
    }

    // FIXME: Should not be needed originally ?? Unless we want to avoid
    // signature collisions with existing disks. We also should not touch
    // non-initialized disks as well...
    UpdateDiskSignatures(List);

    UpdateHwDiskNumbers(List);
    AssignDriveLetters(List);

    /*
     * Retrieve the system partition: the active partition on the system
     * disk (the one that will be booted by default by the hardware).
     */
    SystemDisk = GetSystemDisk(List);
    List->SystemPartition = (SystemDisk ? GetActiveDiskPartition(SystemDisk) : NULL);

    return List;
}

VOID
DestroyPartitionList(
    IN PPARTLIST List)
{
    PDISKENTRY DiskEntry;
    PBIOSDISKENTRY BiosDiskEntry;
    PPARTENTRY PartEntry;
    PLIST_ENTRY Entry;
    UINT i;

    /* Free the disk and partition info */
    while (!IsListEmpty(&List->DiskListHead))
    {
        Entry = RemoveHeadList(&List->DiskListHead);
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        /* Free the driver name */
        RtlFreeUnicodeString(&DiskEntry->DriverName);

        /* Free the partition lists */
        for (i = PRIMARY_PARTITIONS; i <= LOGICAL_PARTITIONS; ++i)
        {
            while (!IsListEmpty(&DiskEntry->PartList[i]))
            {
                Entry = RemoveHeadList(&DiskEntry->PartList[i]);
                PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
                RtlFreeHeap(ProcessHeap, 0, PartEntry);
            }
        }

        /* Free the layout buffer */
        if (DiskEntry->LayoutBuffer != NULL)
            RtlFreeHeap(ProcessHeap, 0, DiskEntry->LayoutBuffer);

        /* Free the disk entry */
        RtlFreeHeap(ProcessHeap, 0, DiskEntry);
    }

    /* Free the bios disk info */
    while (!IsListEmpty(&List->BiosDiskListHead))
    {
        Entry = RemoveHeadList(&List->BiosDiskListHead);
        BiosDiskEntry = CONTAINING_RECORD(Entry, BIOSDISKENTRY, ListEntry);
        RtlFreeHeap(ProcessHeap, 0, BiosDiskEntry);
    }

    /* Free the list head */
    RtlFreeHeap(ProcessHeap, 0, List);
}

PDISKENTRY
GetDiskByBiosNumber(
    IN PPARTLIST List,
    IN ULONG HwDiskNumber)
{
    PDISKENTRY DiskEntry;
    PLIST_ENTRY Entry;

    /* Loop over the disks and find the correct one */
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        if (DiskEntry->HwDiskNumber == HwDiskNumber)
        {
            /* Disk found */
            return DiskEntry;
        }
    }

    /* Disk not found, stop there */
    return NULL;
}

PDISKENTRY
GetDiskByNumber(
    IN PPARTLIST List,
    IN ULONG DiskNumber)
{
    PDISKENTRY DiskEntry;
    PLIST_ENTRY Entry;

    /* Loop over the disks and find the correct one */
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        if (DiskEntry->DiskNumber == DiskNumber)
        {
            /* Disk found */
            return DiskEntry;
        }
    }

    /* Disk not found, stop there */
    return NULL;
}

PDISKENTRY
GetDiskBySCSI(
    IN PPARTLIST List,
    IN USHORT Port,
    IN USHORT Bus,
    IN USHORT Id)
{
    PDISKENTRY DiskEntry;
    PLIST_ENTRY Entry;

    /* Loop over the disks and find the correct one */
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        if (DiskEntry->Port == Port &&
            DiskEntry->Bus  == Bus  &&
            DiskEntry->Id   == Id)
        {
            /* Disk found */
            return DiskEntry;
        }
    }

    /* Disk not found, stop there */
    return NULL;
}

PDISKENTRY
GetDiskBySignature(
    IN PPARTLIST List,
    IN DEVICE_SIGNATURE Signature)
{
    PDISKENTRY DiskEntry;
    PLIST_ENTRY Entry;

    /* Loop over the disks and find the correct one */
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        //
        // FIXME: We can use LONG signatures with GPT disks
        // as well (stored in the protective MBR).
        //
        if ((Signature.Type == SignatureLong) &&
            (DiskEntry->DiskStyle == PARTITION_STYLE_MBR))
        {
            if (DiskEntry->LayoutBuffer->Mbr.Signature == Signature.Long)
            {
                /* Disk found */
                return DiskEntry;
            }
        }
        else if ((Signature.Type == SignatureGuid) &&
                 (DiskEntry->DiskStyle == PARTITION_STYLE_GPT))
        {
            if (IsEqualGUID(&DiskEntry->LayoutBuffer->Gpt.DiskId,
                            &Signature.Guid))
            {
                /* Disk found */
                return DiskEntry;
            }
        }
    }

    /* Disk not found, stop there */
    return NULL;
}

PPARTENTRY
GetPartition(
    // IN PPARTLIST List,
    IN PDISKENTRY DiskEntry,
    IN ULONG PartitionNumber)
{
    PPARTENTRY PartEntry;
    PLIST_ENTRY Entry;
    UINT i;

    /* Fail if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("Disk of style %d is not MBR or GPT and does not support partitions.\n",
                DiskEntry->DiskStyle);
        return NULL;
    }

    /* Disk found, loop over the partitions */
    for (i = PRIMARY_PARTITIONS;
         i <= ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
                  ? LOGICAL_PARTITIONS : PRIMARY_PARTITIONS);
         ++i)
    {
        for (Entry =  DiskEntry->PartList[i].Flink;
             Entry != &DiskEntry->PartList[i];
             Entry =  Entry->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            if (PartEntry->PartitionNumber == PartitionNumber)
            {
                /* Partition found */
                return PartEntry;
            }
        }
    }

    /* The partition was not found on the disk, stop there */
    return NULL;
}

BOOLEAN
GetDiskOrPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber OPTIONAL,
    OUT PDISKENTRY* pDiskEntry,
    OUT PPARTENTRY* pPartEntry OPTIONAL)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry = NULL;

    /* Find the disk */
    DiskEntry = GetDiskByNumber(List, DiskNumber);
    if (!DiskEntry)
        return FALSE;

    /* If we have a partition (PartitionNumber != 0), find it */
    if (PartitionNumber != 0)
    {
        PartEntry = GetPartition(/*List,*/ DiskEntry, PartitionNumber);
        if (!PartEntry)
            return FALSE;
        ASSERT(PartEntry->DiskEntry == DiskEntry);
    }

    /* Return the disk (and optionally the partition) */
    *pDiskEntry = DiskEntry;
    if (pPartEntry) *pPartEntry = PartEntry;
    return TRUE;
}

//
// NOTE: Was introduced broken in r6258 by Casper
//
PPARTENTRY
SelectPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;

    DiskEntry = GetDiskByNumber(List, DiskNumber);
    if (!DiskEntry)
        return NULL;

    PartEntry = GetPartition(/*List,*/ DiskEntry, PartitionNumber);
    if (!PartEntry)
        return NULL;

    ASSERT(PartEntry->DiskEntry == DiskEntry);
    ASSERT(DiskEntry->DiskNumber == DiskNumber);
    ASSERT(PartEntry->PartitionNumber == PartitionNumber);

    return PartEntry;
}

PPARTENTRY
GetNextPartition(
    IN PPARTLIST List,
    IN PPARTENTRY CurrentPart OPTIONAL)
{
    PLIST_ENTRY DiskListEntry;
    PLIST_ENTRY PartListEntry;
    PDISKENTRY CurrentDisk;

    /* Fail if no disks are available */
    if (IsListEmpty(&List->DiskListHead))
        return NULL;

    /* Check for the next usable entry on the current partition's disk */
    if (CurrentPart != NULL)
    {
        CurrentDisk = CurrentPart->DiskEntry;

        /* Check the logical partitions only if the disk is MBR */
        if ((CurrentDisk->DiskStyle == PARTITION_STYLE_MBR) &&
            CurrentPart->LogicalPartition)
        {
            /* Logical partition */

            PartListEntry = CurrentPart->ListEntry.Flink;
            if (PartListEntry != &CurrentDisk->PartList[LOGICAL_PARTITIONS])
            {
                /* Next logical partition */
                CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
                return CurrentPart;
            }
            else
            {
                PartListEntry = CurrentDisk->ExtendedPartition->ListEntry.Flink;
                if (PartListEntry != &CurrentDisk->PartList[PRIMARY_PARTITIONS])
                {
                    CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
                    return CurrentPart;
                }
            }
        }
        else
        {
            /* Primary or extended partition */

            /* Check the extended partition only if the disk is MBR */
            if ((CurrentDisk->DiskStyle == PARTITION_STYLE_MBR) &&
                CurrentPart->IsPartitioned &&
                /*IsContainerPartition(CurrentPart->PartitionType.MbrType)*/
                (CurrentDisk->ExtendedPartition == CurrentPart))
            {
                /* First logical partition */
                PartListEntry = CurrentDisk->PartList[LOGICAL_PARTITIONS].Flink;
                if (PartListEntry != &CurrentDisk->PartList[LOGICAL_PARTITIONS])
                {
                    CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
                    return CurrentPart;
                }
            }
            else
            {
                /* Next primary partition */
                PartListEntry = CurrentPart->ListEntry.Flink;
                if (PartListEntry != &CurrentDisk->PartList[PRIMARY_PARTITIONS])
                {
                    CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
                    return CurrentPart;
                }
            }
        }
    }

    /* Search for the first partition entry on the next disk */
    for (DiskListEntry = (CurrentPart ? CurrentDisk->ListEntry.Flink
                                      : List->DiskListHead.Flink);
         DiskListEntry != &List->DiskListHead;
         DiskListEntry = DiskListEntry->Flink)
    {
        CurrentDisk = CONTAINING_RECORD(DiskListEntry, DISKENTRY, ListEntry);

        PartListEntry = CurrentDisk->PartList[PRIMARY_PARTITIONS].Flink;
        if (PartListEntry != &CurrentDisk->PartList[PRIMARY_PARTITIONS])
        {
            CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
            return CurrentPart;
        }
    }

    return NULL;
}

PPARTENTRY
GetPrevPartition(
    IN PPARTLIST List,
    IN PPARTENTRY CurrentPart OPTIONAL)
{
    PLIST_ENTRY DiskListEntry;
    PLIST_ENTRY PartListEntry;
    PDISKENTRY CurrentDisk;

    /* Fail if no disks are available */
    if (IsListEmpty(&List->DiskListHead))
        return NULL;

    /* Check for the previous usable entry on the current partition's disk */
    if (CurrentPart != NULL)
    {
        CurrentDisk = CurrentPart->DiskEntry;

        /* Check the logical partitions only if the disk is MBR */
        if ((CurrentDisk->DiskStyle == PARTITION_STYLE_MBR) &&
            CurrentPart->LogicalPartition)
        {
            /* Logical partition */

            PartListEntry = CurrentPart->ListEntry.Blink;
            if (PartListEntry != &CurrentDisk->PartList[LOGICAL_PARTITIONS])
            {
                /* Previous logical partition */
                CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
            }
            else
            {
                /* Extended partition */
                CurrentPart = CurrentDisk->ExtendedPartition;
            }
            return CurrentPart;
        }
        else
        {
            /* Primary or extended partition */

            PartListEntry = CurrentPart->ListEntry.Blink;
            if (PartListEntry != &CurrentDisk->PartList[PRIMARY_PARTITIONS])
            {
                CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);

                /* Check the extended partition only if the disk is MBR */
                if ((CurrentDisk->DiskStyle == PARTITION_STYLE_MBR) &&
                    CurrentPart->IsPartitioned &&
                    /*IsContainerPartition(CurrentPart->PartitionType.MbrType)*/
                    (CurrentDisk->ExtendedPartition == CurrentPart))
                {
                    PartListEntry = CurrentDisk->PartList[LOGICAL_PARTITIONS].Blink;
                    CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
                }

                return CurrentPart;
            }
        }
    }

    /* Search for the last partition entry on the previous disk */
    for (DiskListEntry = (CurrentPart ? CurrentDisk->ListEntry.Blink
                                      : List->DiskListHead.Blink);
         DiskListEntry != &List->DiskListHead;
         DiskListEntry = DiskListEntry->Blink)
    {
        CurrentDisk = CONTAINING_RECORD(DiskListEntry, DISKENTRY, ListEntry);

        PartListEntry = CurrentDisk->PartList[PRIMARY_PARTITIONS].Blink;
        if (PartListEntry != &CurrentDisk->PartList[PRIMARY_PARTITIONS])
        {
            CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);

            /* Check the extended partition only if the disk is MBR */
            if ((CurrentDisk->DiskStyle == PARTITION_STYLE_MBR) &&
                CurrentPart->IsPartitioned &&
                /*IsContainerPartition(CurrentPart->PartitionType.MbrType)*/
                (CurrentDisk->ExtendedPartition == CurrentPart))
            {
                PartListEntry = CurrentDisk->PartList[LOGICAL_PARTITIONS].Blink;
                if (PartListEntry != &CurrentDisk->PartList[LOGICAL_PARTITIONS])
                {
                    CurrentPart = CONTAINING_RECORD(PartListEntry, PARTENTRY, ListEntry);
                    return CurrentPart;
                }
            }
            else
            {
                return CurrentPart;
            }
        }
    }

    return NULL;
}

// static
FORCEINLINE
BOOLEAN
IsEmptyLayoutEntry(
    IN PPARTITION_INFORMATION PartitionInfo)
{
    if (PartitionInfo->StartingOffset.QuadPart == 0 &&
        PartitionInfo->PartitionLength.QuadPart == 0)
    {
        return TRUE;
    }

    return FALSE;
}

// static
FORCEINLINE
BOOLEAN
IsSamePrimaryLayoutEntry(
    IN PPARTITION_INFORMATION PartitionInfo,
    IN PDISKENTRY DiskEntry,
    IN PPARTENTRY PartEntry)
{
    if (PartitionInfo->StartingOffset.QuadPart == PartEntry->StartSector.QuadPart * DiskEntry->BytesPerSector &&
        PartitionInfo->PartitionLength.QuadPart == PartEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector)
//        PartitionInfo->PartitionType == PartEntry->PartitionType
    {
        return TRUE;
    }

    return FALSE;
}

static
ULONG
GetPrimaryPartitionCount(
    IN PDISKENTRY DiskEntry)
{
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry;
    ULONG Count = 0;

    /* Fail if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("Disk of style %d is not MBR or GPT and does not support partitions.\n",
                DiskEntry->DiskStyle);
        return 0;
    }

    for (Entry =  DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
         Entry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
         Entry =  Entry->Flink)
    {
        PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
        if (PartEntry->IsPartitioned)
            ++Count;
    }

    return Count;
}

static
ULONG
GetLogicalPartitionCount(
    IN PDISKENTRY DiskEntry)
{
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry;
    ULONG Count = 0;

    /* Fail if the disk is not MBR */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
    {
        DPRINT1("Disk of style %d is not MBR and does not support logical partitions.\n",
                DiskEntry->DiskStyle);
        return 0;
    }

    for (Entry =  DiskEntry->PartList[LOGICAL_PARTITIONS].Flink;
         Entry != &DiskEntry->PartList[LOGICAL_PARTITIONS];
         Entry =  Entry->Flink)
    {
        PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
        if (PartEntry->IsPartitioned)
            ++Count;
    }

    return Count;
}

static
BOOLEAN
ReAllocateLayoutBuffer(
    IN PDISKENTRY DiskEntry)
{
    PDRIVE_LAYOUT_INFORMATION NewLayoutBuffer;
    ULONG NewPartitionCount;
    ULONG CurrentPartitionCount = 0;
    ULONG LayoutBufferSize;
    ULONG i;

    DPRINT1("ReAllocateLayoutBuffer()\n");

    if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
    {
        if (IsNEC_98)
        {
            /* Always 16 entries, not more, not less */
            NewPartitionCount = 16;
        }
        else
        {
            /* Always 4 first entries, plus 4 entries for each logical partition */
            NewPartitionCount = 4 + GetLogicalPartitionCount(DiskEntry) * 4;
        }
    }
    else
    {
        /* For GPT disks, use the actual number of (primary) partitions */
        NewPartitionCount = GetPrimaryPartitionCount(DiskEntry);
    }

    if (DiskEntry->LayoutBuffer)
        CurrentPartitionCount = DiskEntry->LayoutBuffer->PartitionCount;

    DPRINT1("CurrentPartitionCount: %lu ; NewPartitionCount: %lu\n",
            CurrentPartitionCount, NewPartitionCount);

    if (CurrentPartitionCount == NewPartitionCount)
        return TRUE;

    LayoutBufferSize = DRIVE_LAYOUT_INFOEX_SIZE(NewPartitionCount);
    NewLayoutBuffer = RtlReAllocateHeap(ProcessHeap,
                                        HEAP_ZERO_MEMORY,
                                        DiskEntry->LayoutBuffer,
                                        LayoutBufferSize);
    if (NewLayoutBuffer == NULL)
    {
        DPRINT1("Failed to allocate the new layout buffer (size: %lu)\n", LayoutBufferSize);
        return FALSE;
    }

    NewLayoutBuffer->PartitionCount = NewPartitionCount;

    /* If the layout buffer grows, make sure the new (empty) entries are written to the disk */
    if (NewPartitionCount > CurrentPartitionCount)
    {
        for (i = CurrentPartitionCount; i < NewPartitionCount; ++i)
        {
            NewLayoutBuffer->PartitionEntry[i].RewritePartition = TRUE;
        }
    }

    DiskEntry->LayoutBuffer = NewLayoutBuffer;

    return TRUE;
}

static
VOID
UpdateDiskLayout(
    IN PDISKENTRY DiskEntry)
{
    PPARTITION_INFORMATION_EX PartitionInfo;
    PPARTITION_INFORMATION_EX LinkInfo = NULL;
    PLIST_ENTRY ListEntry;
    PPARTENTRY PartEntry;
    LARGE_INTEGER HiddenSectors64;
    ULONG Index;
    ULONG PartitionNumber = 1;

    DPRINT1("UpdateDiskLayout()\n");

    /* Resize the layout buffer if necessary */
    if (ReAllocateLayoutBuffer(DiskEntry) == FALSE)
    {
        DPRINT("ReAllocateLayoutBuffer() failed.\n");
        return;
    }

    /*
     * Since the partitions in our internal partition lists are sorted
     * by their starting position, the resulting layout buffer we build
     * lists the partitions in increasing starting position order.
     * Therefore, the partitions on the disk are also sorted by their
     * starting position.
     */

    /* Update the primary partition table */
    Index = 0;
    for (ListEntry =  DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
         ListEntry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
         ListEntry =  ListEntry->Flink)
    {
        PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

        if (!PartEntry->IsPartitioned)
            continue;

        ASSERT(!IS_PARTITION_UNUSED(PartEntry));

        PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];
        PartEntry->PartitionIndex = Index;

        /* Reset the current partition number only for newly-created (unmounted) partitions */
        if (PartEntry->New)
            PartEntry->PartitionNumber = 0;

        PartEntry->OnDiskPartitionNumber =
            (!IsContainerPartition(PartEntry->PartitionType) ? PartitionNumber : 0);

        if (!IsSamePrimaryLayoutEntry(PartitionInfo, DiskEntry, PartEntry))
        {
            DPRINT1("Updating primary partition entry %lu\n", Index);

            PartitionInfo->PartitionNumber = PartEntry->PartitionNumber;
            PartitionInfo->StartingOffset.QuadPart = PartEntry->StartSector.QuadPart * DiskEntry->BytesPerSector;
            PartitionInfo->PartitionLength.QuadPart = PartEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector;

            if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
            {
                // PartitionInfo->Mbr = PartEntry->PartInfo.Mbr;
                PartitionInfo->Mbr.HiddenSectors = PartEntry->StartSector.LowPart;
                PartitionInfo->Mbr.PartitionType = PartEntry->PartitionType.MbrType;
                PartitionInfo->Mbr.BootIndicator = PartEntry->IsSystemPartition;
                PartitionInfo->Mbr.RecognizedPartition = IsRecognizedPartition(PartEntry->PartitionType.MbrType);
            }
            else // if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
            {
                PartitionInfo->Gpt = PartEntry->PartInfo.Gpt;
                PartitionInfo->Gpt.PartitionType = PartEntry->PartitionType.GptType;
            }

            PartitionInfo->RewritePartition = TRUE;
        }

        if (!IsContainerPartition(PartEntry->PartitionType))
            ++PartitionNumber;

        ++Index;
    }

    /*
     * Extra handling for MBR disks only.
     */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
    {
        if (!IsNEC_98)
        {
            ASSERT(Index <= 4);

            /* Update the logical partition table */
            Index = 4;
            for (ListEntry =  DiskEntry->DiskEntry->PartList[LOGICAL_PARTITIONS].Flink;
                 ListEntry != &DiskEntry->DiskEntry->PartList[LOGICAL_PARTITIONS];
                 ListEntry =  ListEntry->Flink)
            {
                PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

                if (!PartEntry->IsPartitioned)
                    continue;

                ASSERT(!IS_PARTITION_UNUSED(PartEntry));

                PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];
                PartEntry->PartitionIndex = Index;

                /* Reset the current partition number only for newly-created (unmounted) partitions */
                if (PartEntry->New)
                    PartEntry->PartitionNumber = 0;

                PartEntry->OnDiskPartitionNumber = PartitionNumber;

                DPRINT1("Updating logical partition entry %lu\n", Index);

                PartitionInfo->PartitionNumber = PartEntry->PartitionNumber;
                PartitionInfo->StartingOffset.QuadPart = PartEntry->StartSector.QuadPart * DiskEntry->BytesPerSector;
                PartitionInfo->PartitionLength.QuadPart = PartEntry->SectorCount.QuadPart * DiskEntry->BytesPerSector;
                // PartitionInfo->Mbr = PartEntry->PartInfo.Mbr;
                PartitionInfo->Mbr.HiddenSectors = DiskEntry->SectorAlignment;
                PartitionInfo->Mbr.PartitionType = PartEntry->PartitionType.MbrType;
                PartitionInfo->Mbr.BootIndicator = FALSE;
                PartitionInfo->Mbr.RecognizedPartition = IsRecognizedPartition(PartEntry->PartitionType.MbrType);
// #if (NTDDI_VERSION >= NTDDI_WINBLUE)
                // PartitionInfo->Mbr.PartitionId = GUID_NULL;
// #endif
                PartitionInfo->RewritePartition = TRUE;

                /* Fill the link entry of the previous partition entry */
                if (LinkInfo != NULL)
                {
                    LinkInfo->PartitionNumber = 0;
                    LinkInfo->StartingOffset.QuadPart = (PartEntry->StartSector.QuadPart - DiskEntry->SectorAlignment) * DiskEntry->BytesPerSector;
                    LinkInfo->PartitionLength.QuadPart = (PartEntry->StartSector.QuadPart + DiskEntry->SectorAlignment) * DiskEntry->BytesPerSector;
                    HiddenSectors64.QuadPart = PartEntry->StartSector.QuadPart - DiskEntry->SectorAlignment - DiskEntry->ExtendedPartition->StartSector.QuadPart;
                    LinkInfo->Mbr.HiddenSectors = HiddenSectors64.LowPart;
                    LinkInfo->Mbr.PartitionType = PARTITION_EXTENDED;
                    LinkInfo->Mbr.BootIndicator = FALSE;
                    LinkInfo->Mbr.RecognizedPartition = FALSE;
// #if (NTDDI_VERSION >= NTDDI_WINBLUE)
                    // LinkInfo->Mbr.PartitionId = GUID_NULL;
// #endif
                    LinkInfo->RewritePartition = TRUE;
                }

                /* Save a pointer to the link entry of the current partition entry */
                LinkInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index + 1];

                ++PartitionNumber;
                Index += 4;
            }
        }
        else
        {
            ASSERT(Index <= 16);
        }

        /*
         * MBR disks have fixed-sized partition tables, therefore
         * they can have empty partition table slots, that need
         * to be reset.
         */

        /* Wipe unused primary partition entries */
        for (Index = GetPrimaryPartitionCount(DiskEntry);
             Index < (IsNEC_98 ? 16 : 4);
             ++Index)
        {
            DPRINT1("Primary partition entry %lu\n", Index);

            PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];

            if (!IsEmptyLayoutEntry(PartitionInfo))
            {
                DPRINT1("Wiping primary partition entry %lu\n", Index);

                PartitionInfo->PartitionNumber = 0;
                PartitionInfo->StartingOffset.QuadPart = 0;
                PartitionInfo->PartitionLength.QuadPart = 0;
                RtlZeroMemory(&PartitionInfo->Mbr, sizeof(PartitionInfo->Mbr));
                PartitionInfo->RewritePartition = TRUE;
            }
        }

        if (!IsNEC_98)
        {
            /* Wipe unused logical partition entries */
            for (Index = 4; Index < DiskEntry->LayoutBuffer->PartitionCount; ++Index)
            {
                if (Index % 4 >= 2)
                {
                    DPRINT1("Logical partition entry %lu\n", Index);

                    PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];

                    if (!IsEmptyLayoutEntry(PartitionInfo))
                    {
                        DPRINT1("Wiping partition entry %lu\n", Index);

                        PartitionInfo->PartitionNumber = 0;
                        PartitionInfo->StartingOffset.QuadPart = 0;
                        PartitionInfo->PartitionLength.QuadPart = 0;
                        RtlZeroMemory(&PartitionInfo->Mbr, sizeof(PartitionInfo->Mbr));
                        PartitionInfo->RewritePartition = TRUE;
                    }
                }
            }
        }
    }

    DiskEntry->Dirty = TRUE;

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif
}

// Limit the search to within a type (either primary or logical)
static
PPARTENTRY
GetPrevUnpartitionedEntry(
    IN PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    PPARTENTRY PrevPartEntry;
    PLIST_ENTRY ListHead;

    if (PartEntry->LogicalPartition)
        ListHead = &DiskEntry->PartList[LOGICAL_PARTITIONS];
    else
        ListHead = &DiskEntry->PartList[PRIMARY_PARTITIONS];

    if (PartEntry->ListEntry.Blink != ListHead)
    {
        PrevPartEntry = CONTAINING_RECORD(PartEntry->ListEntry.Blink,
                                          PARTENTRY,
                                          ListEntry);
        if (!PrevPartEntry->IsPartitioned)
        {
            ASSERT(IS_PARTITION_UNUSED(PrevPartEntry));
            return PrevPartEntry;
        }
    }

    return NULL;
}

// Limit the search to within a type (either primary or logical)
static
PPARTENTRY
GetNextUnpartitionedEntry(
    IN PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    PPARTENTRY NextPartEntry;
    PLIST_ENTRY ListHead;

    if (PartEntry->LogicalPartition)
        ListHead = &DiskEntry->PartList[LOGICAL_PARTITIONS];
    else
        ListHead = &DiskEntry->PartList[PRIMARY_PARTITIONS];

    if (PartEntry->ListEntry.Flink != ListHead)
    {
        NextPartEntry = CONTAINING_RECORD(PartEntry->ListEntry.Flink,
                                          PARTENTRY,
                                          ListEntry);
        if (!NextPartEntry->IsPartitioned)
        {
            ASSERT(IS_PARTITION_UNUSED(NextPartEntry));
            return NextPartEntry;
        }
    }

    return NULL;
}

BOOLEAN
CreatePrimaryPartition(
    IN PPARTLIST List,
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount,
    IN BOOLEAN AutoCreate)
{
    ERROR_NUMBER Error;

    DPRINT1("CreatePrimaryPartition(%I64u)\n", SectorCount);

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned)
    {
        return FALSE;
    }

    Error = PrimaryPartitionCreationChecks(PartEntry);
    if (Error != NOT_AN_ERROR)
    {
        DPRINT1("PrimaryPartitionCreationChecks() failed with error %lu\n", Error);
        return FALSE;
    }

    /* Initialize the partition entry, inserting a new blank region if needed */
    if (!InitializePartitionEntry(PartEntry, SectorCount, AutoCreate))
        return FALSE;

    ASSERT(PartEntry->LogicalPartition == FALSE);

    UpdateDiskLayout(PartEntry->DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

BOOLEAN
CreateExtendedPartition(
    IN PPARTLIST List,
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount)
{
    ERROR_NUMBER Error;

    DPRINT1("CreateExtendedPartition(%I64u)\n", SectorCount);

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned)
    {
        return FALSE;
    }

    Error = ExtendedPartitionCreationChecks(PartEntry);
    if (Error != NOT_AN_ERROR)
    {
        DPRINT1("ExtendedPartitionCreationChecks() failed with error %lu\n", Error);
        return FALSE;
    }
    ASSERT(PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_MBR);

    /* Initialize the partition entry, inserting a new blank region if needed */
    if (!InitializePartitionEntry(PartEntry, SectorCount, FALSE))
        return FALSE;

    ASSERT(PartEntry->LogicalPartition == FALSE);

    if (PartEntry->StartSector.QuadPart < 1450560)
    {
        /* Partition starts below the 8.4GB boundary ==> CHS partition */
        PartEntry->PartitionType.MbrType = PARTITION_EXTENDED;
    }
    else
    {
        /* Partition starts above the 8.4GB boundary ==> LBA partition */
        PartEntry->PartitionType.MbrType = PARTITION_XINT13_EXTENDED;
    }

    // FIXME? Possibly to make GetNextUnformattedPartition work (i.e. skip the extended partition container)
    PartEntry->New = FALSE;
    PartEntry->FormatState = Formatted;

    PartEntry->DiskEntry->ExtendedPartition = PartEntry;

    AddLogicalDiskSpace(PartEntry->DiskEntry);

    UpdateDiskLayout(PartEntry->DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

BOOLEAN
CreateLogicalPartition(
    IN PPARTLIST List,
    IN OUT PPARTENTRY PartEntry,
    IN ULONGLONG SectorCount,
    IN BOOLEAN AutoCreate)
{
    ERROR_NUMBER Error;

    DPRINT1("CreateLogicalPartition(%I64u)\n", SectorCount);

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned)
    {
        return FALSE;
    }

    Error = LogicalPartitionCreationChecks(PartEntry);
    if (Error != NOT_AN_ERROR)
    {
        DPRINT1("LogicalPartitionCreationChecks() failed with error %lu\n", Error);
        return FALSE;
    }
    ASSERT(PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_MBR);

    /* Initialize the partition entry, inserting a new blank region if needed */
    if (!InitializePartitionEntry(PartEntry, SectorCount, AutoCreate))
        return FALSE;

    ASSERT(PartEntry->LogicalPartition == TRUE);

    UpdateDiskLayout(PartEntry->DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

NTSTATUS
DismountVolume(
    IN PPARTENTRY PartEntry)
{
    NTSTATUS Status;
    NTSTATUS LockStatus;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE PartitionHandle;

    /* Check whether the partition is valid and was mounted by the system */
    if (!PartEntry->IsPartitioned ||
        IsContainerPartition(PartEntry->PartitionType) ||
        !IS_RECOGNIZED_PARTITION(PartEntry->DiskEntry->DiskStyle,
                                 PartEntry->PartitionType) ||
        PartEntry->FormatState == UnknownFormat ||
        // NOTE: If FormatState == Unformatted but *FileSystem != 0 this means
        // it has been usually mounted with RawFS and thus needs to be dismounted.
        !*PartEntry->FileSystem ||
        PartEntry->PartitionNumber == 0)
    {
        /* The partition is not mounted, so just return success */
        return STATUS_SUCCESS;
    }

    ASSERT(!IS_PARTITION_UNUSED(PartEntry));

    /* Open the volume */
    Status = OpenDiskPartition(&PartitionHandle,
                               NULL,
                               PartEntry->DiskEntry->DiskNumber,
                               PartEntry->PartitionNumber,
                               TRUE, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot open volume for dismounting! (Status 0x%lx)\n", Status);
        return Status;
    }

    /* Lock the volume */
    LockStatus = NtFsControlFile(PartitionHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_LOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("WARNING: Failed to lock volume! Operations may fail! (Status 0x%lx)\n", LockStatus);
    }

    /* Dismount the volume */
    Status = NtFsControlFile(PartitionHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_DISMOUNT_VOLUME,
                             NULL,
                             0,
                             NULL,
                             0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to unmount volume (Status 0x%lx)\n", Status);
    }

    /* Unlock the volume */
    LockStatus = NtFsControlFile(PartitionHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_UNLOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Failed to unlock volume (Status 0x%lx)\n", LockStatus);
    }

    /* Close the volume */
    NtClose(PartitionHandle);

    return Status;
}

BOOLEAN
DeletePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    OUT PPARTENTRY* FreeRegion OPTIONAL)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PrevPartEntry;
    PPARTENTRY NextPartEntry;
    PPARTENTRY LogicalPartEntry;
    PLIST_ENTRY Entry;

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned == FALSE)
    {
        return FALSE;
    }

    ASSERT(!IS_PARTITION_UNUSED(PartEntry));

    /* Clear the system partition if it is being deleted */
    if (List->SystemPartition == PartEntry)
    {
        ASSERT(List->SystemPartition);
        List->SystemPartition = NULL;
    }

    DiskEntry = PartEntry->DiskEntry;

    /* Check which type of partition (primary/logical or extended) is being deleted */
    if (DiskEntry->ExtendedPartition == PartEntry)
    {
        /* An extended partition is being deleted: delete all logical partition entries */
        while (!IsListEmpty(&DiskEntry->PartList[LOGICAL_PARTITIONS]))
        {
            Entry = RemoveHeadList(&DiskEntry->PartList[LOGICAL_PARTITIONS]);
            LogicalPartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            /* Dismount the logical partition */
            DismountVolume(LogicalPartEntry);

            /* Delete it */
            RtlFreeHeap(ProcessHeap, 0, LogicalPartEntry);
        }

        DiskEntry->ExtendedPartition = NULL;
    }
    else
    {
        /* A primary partition is being deleted: dismount it */
        DismountVolume(PartEntry);
    }

    /* Adjust the unpartitioned disk space entries */

    /* Get pointer to previous and next unpartitioned entries */
    PrevPartEntry = GetPrevUnpartitionedEntry(PartEntry);
    NextPartEntry = GetNextUnpartitionedEntry(PartEntry);

    if (PrevPartEntry != NULL && NextPartEntry != NULL)
    {
        /* Merge the previous, current and next unpartitioned entries */

        /* Adjust the previous entry length */
        PrevPartEntry->SectorCount.QuadPart += (PartEntry->SectorCount.QuadPart + NextPartEntry->SectorCount.QuadPart);

        /* Remove the current and next entries */
        RemoveEntryList(&PartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, PartEntry);
        RemoveEntryList(&NextPartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, NextPartEntry);

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = PrevPartEntry;
    }
    else if (PrevPartEntry != NULL && NextPartEntry == NULL)
    {
        /* Merge the current and the previous unpartitioned entries */

        /* Adjust the previous entry length */
        PrevPartEntry->SectorCount.QuadPart += PartEntry->SectorCount.QuadPart;

        /* Remove the current entry */
        RemoveEntryList(&PartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, PartEntry);

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = PrevPartEntry;
    }
    else if (PrevPartEntry == NULL && NextPartEntry != NULL)
    {
        /* Merge the current and the next unpartitioned entries */

        /* Adjust the next entry offset and length */
        NextPartEntry->StartSector.QuadPart = PartEntry->StartSector.QuadPart;
        NextPartEntry->SectorCount.QuadPart += PartEntry->SectorCount.QuadPart;

        /* Remove the current entry */
        RemoveEntryList(&PartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, PartEntry);

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = NextPartEntry;
    }
    else
    {
        /* Nothing to merge but change the current entry */
        PartEntry->IsPartitioned = FALSE;
        PartEntry->OnDiskPartitionNumber = 0;
        PartEntry->PartitionNumber = 0;
        // PartEntry->PartitionIndex = 0;
        PartEntry->IsSystemPartition = FALSE;
        RtlZeroMemory(&PartEntry->PartitionType, sizeof(PartEntry->PartitionType));
        PartEntry->FormatState = Unformatted;
        PartEntry->FileSystem[0] = L'\0';
        PartEntry->DriveLetter = 0;
        RtlZeroMemory(PartEntry->VolumeLabel, sizeof(PartEntry->VolumeLabel));

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = PartEntry;
    }

    UpdateDiskLayout(DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

static
BOOLEAN
IsSupportedActivePartition(
    IN PPARTENTRY PartEntry)
{
    /* Check the type and the file system of this partition */

    /*
     * We do not support extended partition containers (on MBR disks) marked
     * as active, and containing code inside their extended boot records.
     */
    if (IsContainerPartition(PartEntry->PartitionType))
    {
        DPRINT1("System partition %lu in disk %lu is an extended partition container?!\n",
                PartEntry->PartitionNumber, PartEntry->DiskEntry->DiskNumber);
        return FALSE;
    }

    /*
     * ADDITIONAL CHECKS / BIG HACK:
     *
     * Retrieve its file system and check whether we have
     * write support for it. If that is the case we are fine
     * and we can use it directly. However if we don't have
     * write support we will need to change the active system
     * partition.
     *
     * NOTE that this is completely useless on architectures
     * where a real system partition is required, as on these
     * architectures the partition uses the FAT FS, for which
     * we do have write support.
     * NOTE also that for those architectures looking for a
     * partition boot indicator is insufficient.
     */
    if (PartEntry->FormatState == Unformatted)
    {
        /* If this partition is mounted, it would use RawFS ("RAW") */
        return TRUE;
    }
    else if ((PartEntry->FormatState == Preformatted) ||
             (PartEntry->FormatState == Formatted))
    {
        ASSERT(*PartEntry->FileSystem);

        /* NOTE: Please keep in sync with the RegisteredFileSystems list! */
        if (wcsicmp(PartEntry->FileSystem, L"FAT")   == 0 ||
            wcsicmp(PartEntry->FileSystem, L"FAT32") == 0 ||
         // wcsicmp(PartEntry->FileSystem, L"NTFS")  == 0 ||
            wcsicmp(PartEntry->FileSystem, L"BTRFS") == 0)
        {
            return TRUE;
        }
        else
        {
            // WARNING: We cannot write on this FS yet!
            DPRINT1("Recognized file system '%S' that doesn't have write support yet!\n",
                    PartEntry->FileSystem);
            return FALSE;
        }
    }
    else // if (PartEntry->FormatState == UnknownFormat)
    {
        ASSERT(!*PartEntry->FileSystem);

        DPRINT1("System partition %lu in disk %lu with no or unknown FS?!\n",
                PartEntry->PartitionNumber, PartEntry->DiskEntry->DiskNumber);
        return FALSE;
    }

    // HACK: WARNING: We cannot write on this FS yet!
    // See fsutil.c:InferFileSystem()
    if (PartEntry->PartitionType == PARTITION_IFS)
    {
        DPRINT1("Recognized file system '%S' that doesn't have write support yet!\n",
                PartEntry->FileSystem);
        return FALSE;
    }

    return TRUE;
}

/*
 * TODO:
 * Check whether we are on a ARC or PC-AT / NEC PC-98 or (u)EFI platform.
 * - If ARC / PC-AT / NEC PC-98, check within MBR disks only.
 * - If (u)EFI, check within GPT disks only.
 */
PPARTENTRY
FindSupportedSystemPartition(
    IN PPARTLIST List,
    IN BOOLEAN ForceSelect,
    IN PDISKENTRY AlternativeDisk OPTIONAL,
    IN PPARTENTRY AlternativePart OPTIONAL)
{
    PLIST_ENTRY ListEntry;
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    PPARTENTRY ActivePartition;
    PPARTENTRY CandidatePartition = NULL;

    /* Check for empty disk list */
    if (IsListEmpty(&List->DiskListHead))
    {
        /* No system partition! */
        ASSERT(List->SystemPartition == NULL);
        goto NoSystemPartition;
    }

    /* Adjust the optional alternative disk if needed */
    if (!AlternativeDisk && AlternativePart)
        AlternativeDisk = AlternativePart->DiskEntry;

    /* Ensure that the alternative partition is on the alternative disk */
    if (AlternativePart)
        ASSERT(AlternativeDisk && (AlternativePart->DiskEntry == AlternativeDisk));

    /* Ensure that the alternative disk is in the list */
    if (AlternativeDisk)
        ASSERT(AlternativeDisk->PartList == List);

    /* Start fresh */
    CandidatePartition = NULL;

//
// Step 1 : Check the system disk.
//

    /*
     * First, check whether the system disk, i.e. the one that will be booted
     * by default by the hardware, contains an active partition. If so this
     * should be our system partition.
     */
    DiskEntry = GetSystemDisk(List);

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("System disk -- GPT-partitioned disk detected, not currently supported by SETUP!\n");
        goto UseAlternativeDisk;
    }

    /* If we have a system partition (in the system disk), validate it */
    ActivePartition = List->SystemPartition;
    if (ActivePartition && IsSupportedActivePartition(ActivePartition))
    {
        CandidatePartition = ActivePartition;

        DPRINT1("Use the current system partition %lu in disk %lu, drive letter %C\n",
                CandidatePartition->PartitionNumber,
                CandidatePartition->DiskEntry->DiskNumber,
                (CandidatePartition->DriveLetter == 0) ? L'-' : CandidatePartition->DriveLetter);

        /* Return the candidate system partition */
        return CandidatePartition;
    }

    /* If the system disk is not the optional alternative disk, perform the minimal checks */
    if (DiskEntry != AlternativeDisk)
    {
        /*
         * No active partition has been recognized. Enumerate all the (primary)
         * partitions in the system disk, excluding the possible current active
         * partition, to find a new candidate.
         */
        for (ListEntry =  DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
             ListEntry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
             ListEntry =  ListEntry->Flink)
        {
            /* Retrieve the partition */
            PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

            /* Skip the current active partition */
            if (PartEntry == ActivePartition)
                continue;

            /* Check if the partition is partitioned and used */
            if (PartEntry->IsPartitioned &&
                !IsContainerPartition(PartEntry->PartitionType))
            {
                ASSERT(!IS_PARTITION_UNUSED(PartEntry));

                /* If we get a candidate active partition in the disk, validate it */
                if (IsSupportedActivePartition(PartEntry))
                {
                    CandidatePartition = PartEntry;
                    goto UseAlternativePartition;
                }
            }

#if 0
            /* Check if the partition is partitioned and used */
            if (!PartEntry->IsPartitioned)
            {
                ASSERT(IS_PARTITION_UNUSED(PartEntry));

                // TODO: Check for minimal size!!
                CandidatePartition = PartEntry;
                goto UseAlternativePartition;
            }
#endif
        }

        /*
         * Still nothing, look whether there is some free space that we can use
         * for the new system partition. We must be sure that the total number
         * of partition is less than the maximum allowed, and that the minimal
         * size is fine.
         */
//
// TODO: Fix the handling of system partition being created in unpartitioned space!!
// --> When to partition it? etc...
//
        if (GetPrimaryPartitionCount(DiskEntry) < 4)
        {
            for (ListEntry =  DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
                 ListEntry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
                 ListEntry =  ListEntry->Flink)
            {
                /* Retrieve the partition */
                PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

                /* Skip the current active partition */
                if (PartEntry == ActivePartition)
                    continue;

                /* Check for unpartitioned space */
                if (!PartEntry->IsPartitioned)
                {
                    ASSERT(IS_PARTITION_UNUSED(PartEntry));

                    // TODO: Check for minimal size!!
                    CandidatePartition = PartEntry;
                    goto UseAlternativePartition;
                }
            }
        }
    }


//
// Step 2 : No active partition found: Check the alternative disk if specified.
//

UseAlternativeDisk:
    if (!AlternativeDisk || (!ForceSelect && (DiskEntry != AlternativeDisk)))
        goto NoSystemPartition;

    if (AlternativeDisk->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("Alternative disk -- GPT-partitioned disk detected, not currently supported by SETUP!\n");
        goto NoSystemPartition;
    }

    if (DiskEntry != AlternativeDisk)
    {
        /* Choose the alternative disk */
        DiskEntry = AlternativeDisk;

        /* If we get a candidate active partition, validate it */
        ActivePartition = GetActiveDiskPartition(DiskEntry);
        if (ActivePartition && IsSupportedActivePartition(ActivePartition))
        {
            CandidatePartition = ActivePartition;
            goto UseAlternativePartition;
        }
    }

    /* We now may have an unsupported active partition, or none */

/***
 *** TODO: Improve the selection:
 *** - If we want a really separate system partition from the partition where
 ***   we install, do something similar to what's done below in the code.
 *** - Otherwise if we allow for the system partition to be also the partition
 ***   where we install, just directly fall down to using AlternativePart.
 ***/

    /* Retrieve the first partition of the disk */
    PartEntry = CONTAINING_RECORD(DiskEntry->PartList[PRIMARY_PARTITIONS].Flink,
                                  PARTENTRY, ListEntry);
    ASSERT(DiskEntry == PartEntry->DiskEntry);

    CandidatePartition = PartEntry;

    //
    // See: https://svn.reactos.org/svn/reactos/trunk/reactos/base/setup/usetup/partlist.c?r1=63355&r2=63354&pathrev=63355#l2318
    //

    /* Check if the disk is new and if so, use its first partition as the active system partition */
    if (DiskEntry->NewDisk)
    {
        // !IsContainerPartition(PartEntry->PartitionType);
        if (!CandidatePartition->IsPartitioned || !CandidatePartition->IsSystemPartition) /* CandidatePartition != ActivePartition */
        {
            ASSERT(DiskEntry == CandidatePartition->DiskEntry);

            DPRINT1("Use new first active system partition %lu in disk %lu, drive letter %C\n",
                    CandidatePartition->PartitionNumber,
                    CandidatePartition->DiskEntry->DiskNumber,
                    (CandidatePartition->DriveLetter == 0) ? L'-' : CandidatePartition->DriveLetter);

            /* Return the candidate system partition */
            return CandidatePartition;
        }

        // FIXME: What to do??
        DPRINT1("NewDisk TRUE but first partition is used?\n");
    }

    /*
     * The disk is not new, check if any partition is initialized;
     * if not, the first one becomes the system partition.
     */
    for (ListEntry =  DiskEntry->PartList[PRIMARY_PARTITIONS].Flink;
         ListEntry != &DiskEntry->PartList[PRIMARY_PARTITIONS];
         ListEntry =  ListEntry->Flink)
    {
        /* Retrieve the partition */
        PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

        /* Check if the partition is partitioned and is used */
        // !IsContainerPartition(PartEntry->PartitionType);
        if (/* PartEntry->IsPartitioned && */
            !IS_PARTITION_UNUSED(PartEntry) || PartEntry->IsSystemPartition)
        {
            break;
        }
    }
    if (ListEntry == &DiskEntry->PartList[PRIMARY_PARTITIONS])
    {
        /*
         * OK we haven't encountered any used and active partition,
         * so use the first one as the system partition.
         */
        ASSERT(DiskEntry == CandidatePartition->DiskEntry);

        DPRINT1("Use first active system partition %lu in disk %lu, drive letter %C\n",
                CandidatePartition->PartitionNumber,
                CandidatePartition->DiskEntry->DiskNumber,
                (CandidatePartition->DriveLetter == 0) ? L'-' : CandidatePartition->DriveLetter);

        /* Return the candidate system partition */
        return CandidatePartition;
    }

    /*
     * The disk is not new, we did not find any actual active partition,
     * or the one we found was not supported, or any possible other candidate
     * is not supported. We then use the alternative partition if specified.
     */
    if (AlternativePart)
    {
        DPRINT1("No valid or supported system partition has been found, use the alternative partition!\n");
        CandidatePartition = AlternativePart;
        goto UseAlternativePartition;
    }
    else
    {
NoSystemPartition:
        DPRINT1("No valid or supported system partition has been found on this system!\n");
        return NULL;
    }

UseAlternativePartition:
    /*
     * We are here because we did not find any (active) candidate system
     * partition that we know how to support. What we are going to do is
     * to change the existing system partition and use the alternative partition
     * (e.g. on which we install ReactOS) as the new system partition.
     * Then we will need to add in FreeLdr's boot menu an entry for booting
     * from the original system partition.
     */
    ASSERT(CandidatePartition);

    DPRINT1("Use alternative active system partition %lu in disk %lu, drive letter %C\n",
            CandidatePartition->PartitionNumber,
            CandidatePartition->DiskEntry->DiskNumber,
            (CandidatePartition->DriveLetter == 0) ? L'-' : CandidatePartition->DriveLetter);

    /* Return the candidate system partition */
    return CandidatePartition;
}

BOOLEAN
SetActivePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    IN PPARTENTRY OldActivePart OPTIONAL)
{
    PPARTITION_INFORMATION_EX PartitionInfo;

    /* Check for empty disk list */
    if (IsListEmpty(&List->DiskListHead))
        return FALSE;

    /* Validate the partition entry */
    if (!PartEntry)
        return FALSE;

    /*
     * If the partition entry is already the system partition, or if it is
     * the same as the old active partition hint the user provided (and if
     * it is already active), just return success.
     */
    if ((PartEntry == List->SystemPartition) ||
        ((PartEntry == OldActivePart) && IsSystemPartition(OldActivePart)))
    {
        return TRUE;
    }

    ASSERT(PartEntry->DiskEntry);

    /* Ensure that the partition's disk is in the list */
    ASSERT(PartEntry->DiskEntry->PartList == List);

    /*
     * If the user provided an old active partition hint, verify that it is
     * indeeed active and belongs to the same disk where the new partition
     * belongs. Otherwise determine the current active partition on the disk
     * where the new partition belongs.
     */
    if (!(OldActivePart && IsSystemPartition(OldActivePart) && (OldActivePart->DiskEntry == PartEntry->DiskEntry)))
    {
        /* It's not, determine the current active partition for the disk */
        OldActivePart = GetActiveDiskPartition(PartEntry->DiskEntry);
    }

    /* Unset the old active partition if it exists */
    if (OldActivePart)
    {
        OldActivePart->IsSystemPartition = FALSE;
        PartitionInfo = GET_PARTITION_LAYOUT_ENTRY(OldActivePart);
        PartitionInfo->Mbr.BootIndicator = FALSE;
        PartitionInfo->RewritePartition = TRUE;
        OldActivePart->DiskEntry->Dirty = TRUE;
    }

    /* Modify the system partition if the new partition is on the system disk */
    if (PartEntry->DiskEntry == GetSystemDisk(List))
        List->SystemPartition = PartEntry;

    /* Set the new active partition */
    PartEntry->IsSystemPartition = TRUE;
    PartitionInfo = GET_PARTITION_LAYOUT_ENTRY(PartEntry);
    PartitionInfo->Mbr.BootIndicator = TRUE;
    PartitionInfo->RewritePartition = TRUE;
    PartEntry->DiskEntry->Dirty = TRUE;

    return TRUE;
}

// CommitChangesToDisk()
NTSTATUS
WritePartitions(
    IN PDISKENTRY DiskEntry)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    IO_STATUS_BLOCK Iosb;
    ULONG BufferSize;
    ULONG PartitionCount;
    UINT i;
    PLIST_ENTRY ListEntry;
    PPARTENTRY PartEntry;

    DPRINT("WritePartitions() Disk: %lu\n", DiskEntry->DiskNumber);

    /* If the disk is not dirty, there is nothing to do */
    if (!DiskEntry->Dirty)
        return STATUS_SUCCESS;

    Status = OpenDiskPartition(&FileHandle,
                               NULL,
                               DiskEntry->DiskNumber, 0,
                               TRUE, FALSE); // Actually, no share flags whatsoever.
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OpenDiskPartition() failed (Status %lx)\n", Status);
        return Status;
    }

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif

    //
    // FIXME: We first *MUST* use IOCTL_DISK_CREATE_DISK to initialize
    // the disk in MBR or GPT format in case the disk was not initialized!!
    // For this we must ask the user which format to use.
    //

    /* Save the original partition count to be restored later (see comment below) */
    PartitionCount = DiskEntry->LayoutBuffer->PartitionCount;

    /* Set the new disk layout and retrieve its updated version with possibly modified partition numbers */
    BufferSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                 ((PartitionCount - 1) * sizeof(PARTITION_INFORMATION_EX));
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                                   DiskEntry->LayoutBuffer,
                                   BufferSize,
                                   DiskEntry->LayoutBuffer,
                                   BufferSize);
    NtClose(FileHandle);

    /*
     * IOCTL_DISK_SET_DRIVE_LAYOUT(_EX) calls IoWritePartitionTable(), which converts
     * DiskEntry->LayoutBuffer->PartitionCount into a partition *table* count,
     * where such a table is expected to enumerate up to 4 partitions:
     * partition *table* count == ROUND_UP(PartitionCount, 4) / 4 .
     * Due to this we need to restore the original PartitionCount number.
     */
    DiskEntry->LayoutBuffer->PartitionCount = PartitionCount;

    /* Check whether the IOCTL_DISK_SET_DRIVE_LAYOUT call succeeded */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_SET_DRIVE_LAYOUT failed (Status 0x%08lx)\n", Status);
        return Status;
    }

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif

    /* Update the partition numbers */
    for (i = PRIMARY_PARTITIONS;
         i <= ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
                  ? LOGICAL_PARTITIONS : PRIMARY_PARTITIONS);
         ++i)
    {
        /* Update the selected partition table */
        for (ListEntry =  DiskEntry->PartList[i].Flink;
             ListEntry != &DiskEntry->PartList[i];
             ListEntry =  ListEntry->Flink)
        {
            PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

            if (PartEntry->IsPartitioned)
            {
                ASSERT(!IS_PARTITION_UNUSED(PartEntry));
                PartEntry->PartitionNumber = GET_PARTITION_LAYOUT_ENTRY(PartEntry)->PartitionNumber;
            }
        }
    }

    //
    // NOTE: Originally (see r40437), we used to install here also a new MBR
    // for this disk (by calling InstallMbrBootCodeToDisk), only if:
    // DiskEntry->NewDisk == TRUE and DiskEntry->HwDiskNumber == 0.
    // Then after that, both DiskEntry->NewDisk and DiskEntry->NoMbr were set
    // to FALSE. In the other place (in usetup.c) where InstallMbrBootCodeToDisk
    // was called too, the installation test was modified by checking whether
    // DiskEntry->NoMbr was TRUE (instead of NewDisk).
    //

    // HACK: Parts of FIXMEs described above: (Re)set the PartitionStyle to MBR.
    DiskEntry->DiskStyle = PARTITION_STYLE_MBR;

    /* The layout has been successfully updated, the disk is not dirty anymore */
    DiskEntry->Dirty = FALSE;

    return Status;
}

// CommitChanges()
BOOLEAN
WritePartitionsToDisk(
    IN PPARTLIST List)
{
    NTSTATUS Status;
    PLIST_ENTRY Entry;
    PDISKENTRY DiskEntry;

    if (List == NULL)
        return TRUE;

    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        if (DiskEntry->Dirty != FALSE)
        {
            Status = WritePartitions(DiskEntry);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("WritePartitionsToDisk() failed to update disk %lu, Status 0x%08lx\n",
                        DiskEntry->DiskNumber, Status);
            }
        }
    }

    return TRUE;
}

BOOLEAN
SetMountedDeviceValue(
    IN WCHAR Letter,
    IN ULONG Signature,
    IN LARGE_INTEGER StartingOffset)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"SYSTEM\\MountedDevices");
    UNICODE_STRING ValueName;
    WCHAR ValueNameBuffer[16];
    HANDLE KeyHandle;
    REG_DISK_MOUNT_INFO MountInfo;

    RtlStringCchPrintfW(ValueNameBuffer, ARRAYSIZE(ValueNameBuffer),
                        L"\\DosDevices\\%c:", Letter);
    RtlInitUnicodeString(&ValueName, ValueNameBuffer);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_LOCAL_MACHINE, NULL),
                               NULL);

    Status = NtOpenKey(&KeyHandle,
                       KEY_ALL_ACCESS,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        Status = NtCreateKey(&KeyHandle,
                             KEY_ALL_ACCESS,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    MountInfo.Signature = Signature;
    MountInfo.StartingOffset = StartingOffset;
    Status = NtSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_BINARY,
                           (PVOID)&MountInfo,
                           sizeof(MountInfo));
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
SetMountedDeviceValues(
    IN PPARTLIST List)
{
    PLIST_ENTRY Entry1, Entry2;
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    UINT i;
    LARGE_INTEGER StartingOffset;

    if (List == NULL)
        return FALSE;

    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        for (i = PRIMARY_PARTITIONS;
             i <= ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
                      ? LOGICAL_PARTITIONS : PRIMARY_PARTITIONS);
             ++i)
        {
            for (Entry2 =  DiskEntry->PartList[i].Flink;
                 Entry2 != &DiskEntry->PartList[i];
                 Entry2 =  Entry2->Flink)
            {
                PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);
                if (PartEntry->IsPartitioned) // && !IsContainerPartition(PartEntry->PartitionType)
                {
                    ASSERT(!IS_PARTITION_UNUSED(PartEntry));

                    /* Assign a "\DosDevices\#:" mount point to this partition */
                    if (PartEntry->DriveLetter)
                    {
                        StartingOffset.QuadPart = PartEntry->StartSector.QuadPart * DiskEntry->BytesPerSector;
                        if (!SetMountedDeviceValue(PartEntry->DriveLetter,
                                                   DiskEntry->LayoutBuffer->Signature,
                                                   StartingOffset))
                        {
                            return FALSE;
                        }
                    }
                }
            }
        }
    }

    return TRUE;
}

VOID
SetMBRPartitionType(
    IN PPARTENTRY PartEntry,
    IN UCHAR PartitionType)
{
    PPARTITION_INFORMATION_EX PartitionInfo;

    ASSERT(PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_MBR);

    PartEntry->PartitionType.MbrType = PartitionType;

    PartEntry->DiskEntry->Dirty = TRUE;
    PartitionInfo = GET_PARTITION_LAYOUT_ENTRY(PartEntry);
    PartitionInfo->Mbr.PartitionType = PartitionType;
    PartitionInfo->Mbr.RecognizedPartition = IsRecognizedPartition(PartitionType);
    PartitionInfo->RewritePartition = TRUE;
}

ERROR_NUMBER
PrimaryPartitionCreationChecks(
    IN PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    /* Fail if the disk is not MBR or GPT */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR &&
        DiskEntry->DiskStyle != PARTITION_STYLE_GPT)
    {
        DPRINT1("Disk of style %d is not MBR or GPT and does not support partitions.\n",
                DiskEntry->DiskStyle);
        return ERROR_WARN_PARTITION;
    }

    /* Fail if the partition is already in use */
    if (PartEntry->IsPartitioned)
        return ERROR_NEW_PARTITION;

    /* Only one primary partition is allowed on super-floppy */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_SUPERFLOPPY)
        return ERROR_PARTITION_TABLE_FULL;

#if 0
    if (DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
    {
        /* Fail if there are already 4 primary partitions in the list */
        if (GetPrimaryPartitionCount(DiskEntry) >= 4)
            return ERROR_PARTITION_TABLE_FULL;
    }
    else // if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        /* Fail if there are already more than the maximum
         * number of primary partitions in the list. */
        ASSERT(DiskEntry->LayoutBuffer);
        if (GetPrimaryPartitionCount(DiskEntry) >= DiskEntry->LayoutBuffer->Gpt.MaxPartitionCount)
            return ERROR_PARTITION_TABLE_FULL;
    }
#else
    /* Fail if there are already more than the maximum
     * number of primary partitions in the list. */
    ASSERT(DiskEntry->LayoutBuffer);
    if (GetPrimaryPartitionCount(DiskEntry) >=
        MAX_PARTITION_ENTRIES_LAYOUT(DiskEntry->LayoutBuffer))
    {
        return ERROR_PARTITION_TABLE_FULL;
    }
#endif

    return ERROR_SUCCESS;
}

ERROR_NUMBER
ExtendedPartitionCreationChecks(
    IN PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    /* Fail if the disk is not MBR */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
    {
        DPRINT1("Disk of style %d is not MBR and does not support extended partitions.\n",
                DiskEntry->DiskStyle);
        return ERROR_WARN_PARTITION;
    }

    /* Fail if the partition is already in use */
    if (PartEntry->IsPartitioned)
        return ERROR_NEW_PARTITION;

    /* Only one primary partition is allowed on super-floppy */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_SUPERFLOPPY)
        return ERROR_PARTITION_TABLE_FULL;

    /* Fail if there are already 4 primary partitions in the list */
    if (GetPrimaryPartitionCount(DiskEntry) >= 4)
        return ERROR_PARTITION_TABLE_FULL;

    /* Fail if there is another extended partition in the list */
    if (DiskEntry->ExtendedPartition != NULL)
        return ERROR_ONLY_ONE_EXTENDED;

    return ERROR_SUCCESS;
}

ERROR_NUMBER
LogicalPartitionCreationChecks(
    IN PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    /* Fail if the disk is not MBR */
    if (DiskEntry->DiskStyle != PARTITION_STYLE_MBR)
    {
        DPRINT1("Disk of style %d is not MBR and does not support logical partitions.\n",
                DiskEntry->DiskStyle);
        return ERROR_WARN_PARTITION;
    }

    /* Fail if the partition is already in use */
    if (PartEntry->IsPartitioned)
        return ERROR_NEW_PARTITION;

    /* Only one primary partition is allowed on super-floppy */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_SUPERFLOPPY)
        return ERROR_PARTITION_TABLE_FULL;

    return ERROR_SUCCESS;
}

BOOLEAN
GetNextUnformattedPartition(
    IN PPARTLIST List,
    OUT PDISKENTRY *pDiskEntry OPTIONAL,
    OUT PPARTENTRY *pPartEntry)
{
    PLIST_ENTRY Entry1, Entry2;
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    UINT i;

    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        for (i = PRIMARY_PARTITIONS;
             i <= ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
                      ? LOGICAL_PARTITIONS : PRIMARY_PARTITIONS);
             ++i)
        {
            for (Entry2 =  DiskEntry->PartList[i].Flink;
                 Entry2 != &DiskEntry->PartList[i];
                 Entry2 =  Entry2->Flink)
            {
                PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);
                if (PartEntry->IsPartitioned && PartEntry->New)
                {
                    ASSERT(DiskEntry == PartEntry->DiskEntry);
                    if (pDiskEntry) *pDiskEntry = DiskEntry;
                    *pPartEntry = PartEntry;
                    return TRUE;
                }
            }
        }
    }

    if (pDiskEntry) *pDiskEntry = NULL;
    *pPartEntry = NULL;

    return FALSE;
}

BOOLEAN
GetNextUncheckedPartition(
    IN PPARTLIST List,
    OUT PDISKENTRY *pDiskEntry OPTIONAL,
    OUT PPARTENTRY *pPartEntry)
{
    PLIST_ENTRY Entry1, Entry2;
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    UINT i;

    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        for (i = PRIMARY_PARTITIONS;
             i <= ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
                      ? LOGICAL_PARTITIONS : PRIMARY_PARTITIONS);
             ++i)
        {
            for (Entry2 =  DiskEntry->PartList[i].Flink;
                 Entry2 != &DiskEntry->PartList[i];
                 Entry2 =  Entry2->Flink)
            {
                PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);
                if (PartEntry->IsPartitioned && PartEntry->NeedsCheck)
                {
                    ASSERT(DiskEntry == PartEntry->DiskEntry);
                    if (pDiskEntry) *pDiskEntry = DiskEntry;
                    *pPartEntry = PartEntry;
                    return TRUE;
                }
            }
        }
    }

    if (pDiskEntry) *pDiskEntry = NULL;
    *pPartEntry = NULL;

    return FALSE;
}


//
// Bootsector routines
//

NTSTATUS
InstallMbrBootCode(
    IN PCWSTR SrcPath,      // MBR source file (on the installation medium)
    IN HANDLE DstPath,      // Where to save the bootsector built from the source + disk information
    IN HANDLE DiskHandle)   // Disk holding the (old) MBR information
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    BOOTCODE OrigBootSector = {0};
    BOOTCODE NewBootSector  = {0};

C_ASSERT(sizeof(PARTITION_SECTOR) == SECTORSIZE);

    /* Allocate and read the current original MBR bootsector */
    Status = ReadBootCodeByHandle(&OrigBootSector,
                                  DiskHandle,
                                  sizeof(PARTITION_SECTOR));
    if (!NT_SUCCESS(Status))
        return Status;

    /* Allocate and read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    Status = ReadBootCodeFromFile(&NewBootSector,
                                  &Name,
                                  sizeof(PARTITION_SECTOR));
    if (!NT_SUCCESS(Status))
    {
        FreeBootCode(&OrigBootSector);
        return Status;
    }

    /*
     * Copy the disk signature, the reserved fields and
     * the partition table from the old MBR to the new one.
     */
    RtlCopyMemory(&((PPARTITION_SECTOR)NewBootSector.BootCode)->Signature,
                  &((PPARTITION_SECTOR)OrigBootSector.BootCode)->Signature,
                  sizeof(PARTITION_SECTOR) -
                  FIELD_OFFSET(PARTITION_SECTOR, Signature)
                  /* Length of partition table */);

    /* Free the original bootsector */
    FreeBootCode(&OrigBootSector);

    /* Write the new bootsector to DstPath */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector.BootCode,
                         NewBootSector.Length,
                         &FileOffset,
                         NULL);

    /* Free the new bootsector */
    FreeBootCode(&NewBootSector);

    return Status;
}

/* EOF */
