/*
 * PROJECT:     FreeLoader
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Expand paths containing user-specified device path aliases
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

//#include <freeldr.h>

#ifdef FW_FRLDR

/**
 * @brief
 * An entry mapping a path alias to a corresponding ARC path.
 *
 * Examples:
 *
 * Alias path --> ARC path
 *      "fd0"     "multi(0)disk(0)fdisk(0)"
 *       "A:"     "multi(0)disk(0)fdisk(0)"
 *       "C:"     "multi(0)disk(0)rdisk(0)partition(1)"
 *      "CD:"     "scsi(0)cdrom(1)fdisk(0)"
 **/
typedef struct _PATH_MAPPING
{
    LIST_ENTRY ListEntry;
    PCSTR AliasPath;
    PCSTR ArcPath;
} PATH_MAPPING, *PPATH_MAPPING;

LIST_ENTRY PathMapList = {&PathMapList, &PathMapList};

#define TAG_PATH_PREFIX 'xfpP'

VOID
InitPathMap(VOID)
{
    InitializeListHead(&PathMapList);
}

VOID
FreePathMap(VOID)
{
    PLIST_ENTRY ListEntry;
    PVOID Entry;

    while (!IsListEmpty(&PathMapList))
    {
        ListEntry = RemoveHeadList(&PathMapList);
        Entry = (PVOID)CONTAINING_RECORD(ListEntry, PATH_MAPPING, ListEntry);
        FrLdrHeapFree(Entry, TAG_PATH_PREFIX);
    }
}

BOOLEAN
AddPathMapEntry(
    _In_ PCSTR AliasPath,
    _In_ PCSTR ArcPath)
{
    PPATH_MAPPING PathMapEntry;
    PSTR AliasPathPtr;
    PSTR ArcPathPtr;
    ULONG AliasPathLen = strlen(AliasPath) + 1;
    ULONG ArcPathLen = strlen(ArcPath) + 1;

    PathMapEntry = FrLdrHeapAlloc(sizeof(PATH_MAPPING) +
                                  AliasPathLen + ArcPathLen,
                                  TAG_PATH_PREFIX);
    if (!PathMapEntry)
        return FALSE;

    AliasPathPtr = (PSTR)((ULONG_PTR)PathMapEntry + sizeof(PATH_MAPPING));
    ArcPathPtr   = (PSTR)((ULONG_PTR)PathMapEntry + sizeof(PATH_MAPPING) + AliasPathLen);

    PathMapEntry->AliasPath = AliasPathPtr;
    PathMapEntry->ArcPath = ArcPathPtr;

    RtlCopyMemory(AliasPathPtr, AliasPath, AliasPathLen);
    RtlCopyMemory(ArcPathPtr, ArcPath, ArcPathLen);

    InsertTailList(&PathMapList, &PathMapEntry->ListEntry);
    return TRUE;
}

/*****
{
    // Find existence of first two floppy disks, and if so:
    AddPathMapEntry("A:", "multi(0)disk(0)fdisk(0)");
    AddPathMapEntry("B:", "multi(0)disk(0)fdisk(1)");

    // Find existence of hard disks and if so:
    AddPathMapEntry("C:", "multi(0)disk(0)rdisk(0)partition(1)");
    // etc.

    // Find existence of CD-ROMs and if so:
    AddPathMapEntry("CD:", "multi(0)cdrom(0)fdisk(0)");
}
*****/

#endif /* FW_FRLDR */


/**
 * @brief
 * Converts a short string representation of a drive and partition
 * to their corresponding numbers.
 *
 * @param[in]   DriveString
 * The short string representation of a drive and partition.
 * The recognized format is:
 *
 *     dev#[,part#]
 *
 * where:
 *
 * - dev# is either a drive number (in decimal or hexadecimal),
 *   or a zero-based identifier for the following disk types:
 *   Floppy disks: fd#, Hard disks: hd#, CD-ROMs: cd#, Ramdisks: rd#.
 *
 * - part# is an optional partition (1-based) if the drive is accessed
 *   in partitioned mode. If part# is omitted or set to zero, the drive
 *   is accessed in non-partitioned mode.
 *
 * @param[out]  Trailing
 * Optional pointer to a variable that receives the rest of the string
 * after the drive and partition string representation.
 *
 * @param[out]  DriveType
 * Recognized drive type: the first letter of the identifier
 * described above, or NUL if unspecified.
 *
 * @param[out]  DriveNumber
 * Corresponding drive number.
 *
 * @param[out]  PartitionNumber
 * Optional drive partition number (or zero if none).
 *
 * @param[out]  HwDriveNumber
 * Optional platform hardware-specific drive number.
 *
 * @return  TRUE if successful, FALSE if not.
 **/
BOOLEAN
DriveStrToNumber(
    _In_ PCSTR DriveString,
    _Out_opt_ PCSTR* Trailing,
    _Out_ PUCHAR DriveType,
    _Out_ PULONG DriveNumber,
    _Out_opt_ PULONG PartitionNumber,
    _Out_opt_ PULONG HwDriveNumber)
{
    PCCH p = DriveString;
    ULONG Drive = 0;
    ULONG Partition = 0;

    if (!*p)
        return FALSE;

    /* Retrieve the "drive type" specified by the first letter, when
     * drives are specified in the form "fd#", "hd#", "cd#" or "rd#" */
    if (p[0] && (tolower(p[1]) == 'd'))
    {
        UCHAR Type = tolower(p[0]);
        p += 2;

        /* Check for recognized drive type */
        if (Type != 'f' && Type != 'h' && Type != 'c' && Type != 'r')
            return FALSE;

        /* The drive number must follow in decimal only. The other
         * characters are validated with the strtoul() call below. */
        if (*p < '0' || *p > '9')
            return FALSE;
        Drive = strtoul(p, (PCHAR*)&p, 10);
        if (*p && *p != ',')
            return FALSE;

        if (Type == 'f')
        {
            /* Floppies always have one single partition */
            Partition = 1;
        }

        *DriveType = Type;
    }
    else
    /* A drive number is directly specified, just return it */
    {
        Drive = strtoul(p, (PCHAR*)&p, 0); // Decimal or hexadecimal
        if (*p && *p != ',')
            return FALSE;
        *DriveType = 0;
    }

    if (*p)
    {
        if (*p != ',')
            return FALSE;
        Partition = strtoul(p+1, (PCHAR*)&p, 0);
    }

    if (Trailing)
        *Trailing = p;

#if (defined(_M_IX86) || defined(_M_AMD64)) && !defined(UEFIBOOT)
/* Extra conversion and validation for BIOS-based PC builds *ONLY*
 * This is mostly to handle the case where a drive number was
 * directly specified, which is a platform-specific feature, where
 * *DriveType == 0 */
    {
    UCHAR BiosDriveNumber; // Dummy variable here...
    if (!DriveToBIOSNumber(DriveType,
                           Drive,
                           &BiosDriveNumber,
                           &Partition))
    {
        return FALSE;
    }
    if (HwDriveNumber)
        *HwDriveNumber = (ULONG)BiosDriveNumber;
    }
#else
    if (HwDriveNumber)
        *HwDriveNumber = 0;
#endif /* (_M_IX86 || _M_AMD64) && !UEFIBOOT */

    *DriveNumber = Drive;
    if (PartitionNumber)
        *PartitionNumber = Partition;
    return TRUE;
}

/**
 * @brief
 * Construct an ARC path, corresponding to a given drive and partition number,
 * usually retrieved from a previous call to DriveStrToNumber(). An optional
 * sub-path component can be given to complete the ARC path.
 *
 * @param[out]  ArcPath
 * Pointer to a buffer that will receive, in output, the ARC path.
 *
 * @param[in]   BufferSize
 * The maximum size of the buffer pointed by ArcPath.
 *
 * @param[in]   DriveType
 * Recognized drive type.
 *
 * @param[in]   DriveNumber
 * Corresponding drive number.
 *
 * @param[in]   PartitionNumber
 * Drive partition number (or zero if none).
 *
 * @return  None.
 * @see DriveStrToNumber().
 **/
static BOOLEAN
ConstructArcPath(
    _Out_ PSTR ArcPath,
    _In_ ULONG BufferSize,
    _In_ UCHAR DriveType,
    _In_ ULONG DriveNumber,
    _In_ ULONG PartitionNumber)
{
    NTSTATUS Status;
    CHAR tmp[50];

    if (DriveType == 'r')
    {
        /*
         * Ramdisk path:
         * ramdisk(x)\path
         */
        Status = RtlStringCbPrintfA(ArcPath, BufferSize, "ramdisk(%lu)", DriveNumber);
        return NT_SUCCESS(Status);
    }

    /*
     * Assume this is going to be on multi()disk().
     * TODO: In the future, maintain an ARC mapping of the hdX <-> ARC paths
     * (for example) so as to always have the correct result.
     */
    Status = RtlStringCbCopyA(ArcPath, BufferSize, "multi(0)disk(0)");
    if (!NT_SUCCESS(Status))
        return FALSE;

    if (DriveType == 'f')
    {
        /*
         * Floppy disk path:
         * multi(0)disk(0)fdisk(x)\path
         */
        Status = RtlStringCbPrintfA(tmp, sizeof(tmp), "fdisk(%lu)", DriveNumber);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlStringCbCatA(ArcPath, BufferSize, tmp);
    }
    else if (DriveType == 'h')
    {
        /*
         * Hard disk path:
         * multi(0)disk(0)rdisk(x)partition(y)\path
         */
        Status = RtlStringCbPrintfA(tmp, sizeof(tmp), "rdisk(%lu)partition(%lu)",
                                    DriveNumber, PartitionNumber);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlStringCbCatA(ArcPath, BufferSize, tmp);
    }
    else if (DriveType == 'c')
    {
        /*
         * CD-ROM disk path:
         * multi(0)disk(0)cdrom(x)\path
         */
        Status = RtlStringCbPrintfA(tmp, sizeof(tmp), "cdrom(%lu)", DriveNumber);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlStringCbCatA(ArcPath, BufferSize, tmp);
    }

    return NT_SUCCESS(Status);
}


/**
 * @brief
 * Try to replace in-place the first characters ("prefix")
 * in a source string buffer by a replacement string.
 **/
static BOOLEAN
ReplacePrefixInPlace(
    _Inout_ PSTR SourceBuffer,
    _In_ SIZE_T SourceSize,
    _In_ SIZE_T PrefixLength,
    _In_ PCSTR ReplaceString/*,
    _In_ SIZE_T ReplaceSize*/)
{
    /* String sizes without their NUL terminator */
    SIZE_T SourceStringSize = strlen(SourceBuffer);
    SIZE_T ReplaceSize = strlen(ReplaceString);

    /* Check that the new replaced string would hold in the buffer.
     * NOTE: SourceStringSize - PrefixLength is the length of the
     * remaining string that follows the prefix. */
    if (ReplaceSize + SourceStringSize - PrefixLength + 1 > SourceSize)
        return FALSE;

    /* Move the remaining string to its new location, and copy
     * the replacement string (without its NUL terminator) */
    RtlMoveMemory(SourceBuffer + ReplaceSize,
                  SourceBuffer + PrefixLength,
                  SourceStringSize - PrefixLength + 1);
    RtlCopyMemory(SourceBuffer, ReplaceString, ReplaceSize);
    return TRUE;
}

/**
 * @brief
 * Expands the given path in-place, replacing any path prefix found in it.
 *
 * @param[in]   PathBuffer
 * Buffer to the path prefixed with a path alias that needs to be expanded.
 *
 * @param[in]   Size
 * Maximum size of the buffer pointed by PathBuffer.
 *
 * @return
 * TRUE if the path has been successfully expanded, FALSE if not.
 **/
BOOLEAN
ExpandPath(
    _Inout_ PSTR PathBuffer,
    _In_ ULONG Size)
{
#ifdef FW_FRLDR

    STRING Prefix, Path;
    PLIST_ENTRY Entry;
    PPATH_MAPPING PathMapEntry;

    RtlInitString(&Path, PathBuffer);
    ASSERT(Path.Length + 1 <= Size);

    /*
     * 1. Expand any user-specified prefixes.
     */
    for (Entry = PathMapList.Flink;
         Entry != &PathMapList;
         Entry = Entry->Flink)
    {
        PathMapEntry = CONTAINING_RECORD(Entry, PATH_MAPPING, ListEntry);

        RtlInitString(&Prefix, PathMapEntry->AliasPath);
        if (RtlPrefixString(&Prefix, &Path, TRUE))
        {
            /* Prefix found, try to expand the path in-place */
            // SIZE_T ArcPathLen = strlen(PathMapEntry->ArcPath);
            if (!ReplacePrefixInPlace(PathBuffer,
                                      Size,
                                      Prefix.Length,
                                      PathMapEntry->ArcPath/*,
                                      ArcPathLen*/))
            {
                return FALSE;
            }
            break;
        }
    }

#endif /* FW_FRLDR */

    /*
     * 2. Now, check for any algorithmically-computed "dev#,part#" prefixes.
     */
    {
    PCSTR Trailing;
    UCHAR DriveType;
    ULONG DriveNumber = 0;
    ULONG PartitionNumber = 0;
    CHAR DeviceArcPath[100]; // Should be more than enough!

    if (DriveStrToNumber(PathBuffer,
                         &Trailing,
                         &DriveType,
                         &DriveNumber,
                         &PartitionNumber,
                         NULL))
    {
        SIZE_T PrefixLen = Trailing - PathBuffer;

        /* Construct the corresponding device ARC path */
        if (!ConstructArcPath(DeviceArcPath, sizeof(DeviceArcPath),
                              DriveType, DriveNumber, PartitionNumber))
        {
            return FALSE;
        }

        /* Prefix found, try to expand the path in-place */
        if (!ReplacePrefixInPlace(PathBuffer,
                                  Size,
                                  PrefixLen,
                                  DeviceArcPath/*,
                                  strlen(DeviceArcPath)*/))
        {
            return FALSE;
        }
    }
    }

    /* No prefix found in the path, everything is fine */
    return TRUE;
}

/* EOF */
