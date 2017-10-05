/*
 * PROJECT:         ReactOS VHD File Library
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            sdk/lib/vhdlib/vhdlib.h
 * PURPOSE:         Virtual Hard Disk Format.
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include "vhdlib.h"
#include <ndk/rtlfuncs.h>

#define NDEBUG
#include <debug.h>


/* HELPER FUNCTIONS ***********************************************************/


/**
 * FIXME: This should be inside a library, the latter being platform-dependent
 * and/or provide a fallback default implementation.
 **/
// Taken from reactos/sdk/lib/rtl/actctx.c, line l3690.
// See also reactos/ntoskrnl/ex/uuid.c,
// reactos/dll/win32/rpcrt4/rpcrt4_main.c (UuidCreate)
void uuid_generate(uuid_t* guid)
{
    static ULONG seed = 0;

    ULONG *ptr = (ULONG*)guid;
    int i;

    /* GUID is 16 bytes long */
    for (i = 0; i < sizeof(GUID)/sizeof(ULONG); i++, ptr++)
        *ptr = RtlUniform(&seed);

    ++seed;

    guid->Data3 &= 0x0fff;
    guid->Data3 |= (4 << 12);
    guid->Data4[0] &= 0x3f;
    guid->Data4[0] |= 0x80;
}


#if 0
/*
 * Calculates the number of cylinders, heads and sectors per cylinder
 * based on a given number of sectors. This is the algorithm described
 * in the VHD specification.
 *
 * Note that the geometry doesn't always exactly match total_sectors but
 * may round it down.
 */
static VOID
calculate_geometry(ULONG64 total_sectors, PUINT16 cyls,
                   PUINT8 heads, PUINT8 secs_per_cyl)
{
    ULONG cyls_times_heads;

    /* ATA disks limited to 127 GB */
    // VHD_MAX_GEOMETRY == 65535 * 16 * 255
    total_sectors = min(total_sectors, VHD_MAX_GEOMETRY);

    if (total_sectors > 65535 * 16 * 63)
    {
        *secs_per_cyl = 255;
        *heads = 16;
        cyls_times_heads = total_sectors / *secs_per_cyl;
    }
    else
    {
        *secs_per_cyl = 17;
        cyls_times_heads = total_sectors / *secs_per_cyl;
        *heads = (cyls_times_heads + 1023) / 1024;

        if (*heads < 4)
            *heads = 4;

        if (cyls_times_heads >= (*heads * 1024) || *heads > 16)
        {
            *secs_per_cyl = 31;
            *heads = 16;
            cyls_times_heads = total_sectors / *secs_per_cyl;
        }

        if (cyls_times_heads >= (*heads * 1024))
        {
            *secs_per_cyl = 63;
            *heads = 16;
            cyls_times_heads = total_sectors / *secs_per_cyl;
        }
    }

    *cyls = cyls_times_heads / *heads;
}
#endif

static ULONG
VhdComputeChecksum(
    IN PVOID  Buffer,
    IN SIZE_T BufSize)
{
    ULONG checksum = 0;

    while (BufSize-- > 0)
    {
        checksum += (ULONG)*(PUCHAR)Buffer;
        Buffer = (PVOID)((PUCHAR)Buffer + 1);
    }

    return ~checksum;
}

/*static*/
FORCEINLINE VOID
VhdGetBATEntry(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset,
    OUT PULONG /*PUINT32*/ Sector,
    OUT PULONG /*PUINT32*/ SectorOffset,
    OUT PULONG /*PUINT32*/ BatEntry,
    OUT PULONG /*PUINT32*/ BatEntryIndex)
{
    // ULONG TempBatEntry, TempBatEntryIndex;

    UINT32 VhdSector = ByteOffset->QuadPart / VHD_SECTOR_SIZE;
    *Sector = VhdSector;
    *SectorOffset = ByteOffset->QuadPart % VHD_SECTOR_SIZE;
    *BatEntry = VhdSector / VhdFile->BlockSectors;
    *BatEntryIndex = VhdSector % VhdFile->BlockSectors;

    // TempBatEntry = ByteOffset->QuadPart / VhdFile->BlockSize;
    // TempBatEntryIndex = ByteOffset->QuadPart % VhdFile->BlockSize;

    // // TempBatEntryIndex = TempBatEntryIndex;

    // *SectorOffset = 0;
    // *BatEntry = TempBatEntry;
    // *BatEntryIndex = TempBatEntryIndex;

    // VhdFile->BlockAllocationTable[*BatEntry] + VhdFile->BlockBitmapSize + *BatEntryIndex
}


/* OPENING DISK IMAGES ********************************************************/

static NTSTATUS
VhdOpenDiskNone(
    IN PVHDFILE VhdFile)
{
    return STATUS_SUCCESS;
}

static NTSTATUS
VhdOpenFixedDisk(
    IN PVHDFILE VhdFile)
{
    PVHD_FOOTER Footer;
    Footer = &VhdFile->Footer;

    if (Footer->data_offset != ~0ULL)
    {
        DPRINT1("OpenHDD: Unexpected data offset for VHD HDD fixed image.");
        return FALSE;
    }
    if (Footer->orig_size != Footer->current_size)
    {
        DPRINT1("OpenHDD: VHD HDD fixed image size should be the same as its original size.");
        return FALSE;
    }

    /* Found, open it */

    return STATUS_SUCCESS;
}

static NTSTATUS
VhdOpenDynamicDisk(
    IN PVHDFILE VhdFile)
{
    NTSTATUS Status;
    LARGE_INTEGER FileOffset;
    ULONG BytesToRead;
    VHD_DYNAMIC_HEADER DynHeader;
    ULONG checksum, i;

    /* Read the VHD dynamic disk header */
    FileOffset.QuadPart = BE64_TO_HOST(VhdFile->Footer.data_offset);
    BytesToRead = sizeof(VHD_DYNAMIC_HEADER);
    Status = VhdFile->FileRead(VhdFile,
                               &FileOffset,
                               &DynHeader,
                               BytesToRead,
                               &BytesToRead);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileRead() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    if (RtlCompareMemory(DynHeader.magic, "cxsparse",
                         sizeof(DynHeader.magic)) != sizeof(DynHeader.magic))
    {
        DPRINT1("OpenHDD: Invalid HDD dynamic image (expected VHD).");
        return FALSE;
    }
    if (BE32_TO_HOST(DynHeader.version) != 0x00010000)
    {
        DPRINT1("OpenHDD: VHD HDD dynamic image of unexpected version %d.", DynHeader.version);
        return FALSE;
    }

    /* Verify checksum */
    checksum = BE32_TO_HOST(DynHeader.checksum);
    DynHeader.checksum = 0;
    if (VhdComputeChecksum(&DynHeader, sizeof(DynHeader)) != checksum)
    {
        DPRINT1("OpenHDD: VHD HDD dynamic header checksum does not match.");
        return FALSE;
    }

    if (DynHeader.data_offset != ~0ULL)
    {
        DPRINT1("OpenHDD: Unexpected data offset for VHD HDD dynamic image.");
        return FALSE;
    }

    /* Retrieve the data block size information */
    VhdFile->BlockSize = BE32_TO_HOST(DynHeader.block_size); // TODO: Check whether it's a power of 2??
    VhdFile->BlockSectors = VhdFile->BlockSize / VHD_SECTOR_SIZE;

    /* Compute the size (sector-aligned) of the block bitmaps (same for all the blocks) */
    VhdFile->BlockBitmapSize  = ROUND_UP(VhdFile->BlockSectors / 8, VHD_SECTOR_SIZE); // In bytes
    VhdFile->BlockBitmapSize /= VHD_SECTOR_SIZE; // In number of sectors

    /* Allocate the cache block bitmap (size multiple of sizeof(ULONG), OK for RTL Bitmap API) */
    VhdFile->BlockBitmapBuffer =
        VhdFile->Allocate(VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE, HEAP_ZERO_MEMORY, 0);
    if (!VhdFile->BlockBitmapBuffer)
    {
        return FALSE;
    }
    RtlInitializeBitMap(&VhdFile->BlockBitmap,
                        VhdFile->BlockBitmapBuffer,
                        VhdFile->BlockSectors);
    // RtlClearAllBits(&VhdFile->BlockBitmap);

    /* Allocate & cache the Block Allocation Table */
    VhdFile->BlockAllocationTableEntries = BE32_TO_HOST(DynHeader.max_table_entries);
    // This should be equal to the number of blocks in the disk (that is, the disk size divided by the block size).

    VhdFile->BlockAllocationTable =
        VhdFile->Allocate(VhdFile->BlockAllocationTableEntries * sizeof(ULONG),
                          HEAP_ZERO_MEMORY, 0);
    if (!VhdFile->BlockAllocationTable)
    {
        VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);
        return FALSE;
    }

    /* Read the Block Allocation Table */
    VhdFile->BlockAllocationTableOffset = BE64_TO_HOST(DynHeader.table_offset);
    FileOffset.QuadPart = VhdFile->BlockAllocationTableOffset;
    BytesToRead = VhdFile->BlockAllocationTableEntries * sizeof(ULONG);
    Status = VhdFile->FileRead(VhdFile,
                               &FileOffset,
                               VhdFile->BlockAllocationTable,
                               BytesToRead,
                               &BytesToRead);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileRead() failed when reading HDD footer (Status 0x%08lx)\n", Status);
        VhdFile->Free(VhdFile->BlockAllocationTable, 0, 0);
        VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);
        return Status;
    }
    /*
     * Because the offset entries inside the BAT are stored in big endian,
     * we need to convert them into host endian.
     */
    for (i = 0; i < VhdFile->BlockAllocationTableEntries; ++i)
        VhdFile->BlockAllocationTable[i] = BE32_TO_HOST(VhdFile->BlockAllocationTable[i]);

    return STATUS_SUCCESS;
}

static NTSTATUS
VhdOpenDiffDisk(
    IN PVHDFILE VhdFile)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}


/* READING DISK IMAGES ********************************************************/

static NTSTATUS
VhdReadDiskNone(
    IN  PVHDFILE VhdFile,
    OUT PVOID Buffer,
    IN  ULONG Length, // SIZE_T
    IN  PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    /* Just zero out the memory region */
    RtlZeroMemory(Buffer, Length);
    return STATUS_SUCCESS;
}

static NTSTATUS
VhdReadFixedDisk(
    IN  PVHDFILE VhdFile,
    OUT PVOID Buffer,
    IN  ULONG Length, // SIZE_T
    IN  PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    NTSTATUS Status;
    // LARGE_INTEGER FileOffset;

    /* Directly read the data */
    Status = VhdFile->FileRead(VhdFile,
                               ByteOffset,
                               Buffer,
                               Length,
                               &Length);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileRead() failed (Status 0x%08lx)\n", Status);

    // TODO: Return the number of bytes actually read??

    return Status;
}

static NTSTATUS
VhdReadDynamicDisk(
    IN  PVHDFILE VhdFile,
    OUT PVOID Buffer,
    IN  ULONG Length, // SIZE_T
    IN  PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG BufSize, ReadLength; // SIZE_T
    LARGE_INTEGER ReadOffset, FileOffset;
    ULONG Sector, SectorOffset, BatEntry, BatEntryIndex;
    ULONG RunStart, RunSize;
    BOOLEAN IsSectorValid;

    FileOffset = *ByteOffset;

    while (Length > 0)
    {
        /* Get the next data block */
        VhdGetBATEntry(VhdFile, &FileOffset,
                       &Sector, &SectorOffset,
                       &BatEntry, &BatEntryIndex);
        // FIXME: 'Sector' (corresponding to the sector number viewed from the host OS) is unused

        // TODO: Store VhdFile->BlockAllocationTable[BatEntry] in a separate local variable??

        /* Clip the read range to remain in this data block */
        ReadLength = min(Length, (VhdFile->BlockSize - (BatEntryIndex * VHD_SECTOR_SIZE) - SectorOffset));

        /* If the block is not allocated the content of the entry is ~0 */
        if (VhdFile->BlockAllocationTable[BatEntry] == ~0UL)
        {
            /* Just zero out the memory region */
            RtlZeroMemory(Buffer, ReadLength);
        }
        else
        {
            /* Perform the read proper */
            PVOID OldBuffer = Buffer;
            ULONG OldReadLength = ReadLength;

            /* Load the bitmap of this block */
            ReadOffset.QuadPart = (VhdFile->BlockAllocationTable[BatEntry] * VHD_SECTOR_SIZE);
            BufSize = (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE);
            Status = VhdFile->FileRead(VhdFile,
                                       &ReadOffset,
                                       VhdFile->BlockBitmapBuffer,
                                       BufSize,
                                       &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileRead() failed to read block bitmap (Status 0x%08lx)\n", Status);

#if 0
            RtlInitializeBitMap(&VhdFile->BlockBitmap,
                                VhdFile->BlockBitmapBuffer,
                                VhdFile->BlockSectors);
#endif

            /*
             * Loop over each sector in the data block's bitmap:
             * - If 0, the sector is empty (dynamic disk), or the data should
             *   be read from the parent disk (differencing disk).
             * - If 1, the sector contains data (dynamic disk), or the data
             *   should be read from the differencing disk.
             *
             * Sector == Index in the sector bitmap.
             *
             * Note that technically, when reading a dynamic image (only),
             * it is supposed that a bit == 0 corresponds to an empty sector,
             * i.e. a zeroed sector. We could then suppose this is indeed
             * the case, and read at once the full set of sectors contained
             * in the data block, hoping that the empty sectors (as indicated
             * by the bitmap) are really empty.
             * For this version of the reader, we do not suppose so, therefore
             * we separately read the sectors with bit == 1, and the sectors
             * with bit == 0 are manually read as zero data.
             */

            // /* Bias the read offset for the first sector to be read */
            // ReadOffset.QuadPart = SectorOffset;

            for (RunStart = BatEntryIndex;
                 (ReadLength > 0) && (BatEntryIndex < VhdFile->BlockSectors);
                 BatEntryIndex = RunStart)
            {
                IsSectorValid = !!RtlCheckBit(&VhdFile->BlockBitmap, BatEntryIndex); // Inline version of RtlTestBit
                if (IsSectorValid)
                {
                    /*
                     * Find the first next clear bit. The run of set bits then
                     * starts with the current sector and ends at this clear bit.
                     */
                    if (RtlFindNextForwardRunClear(&VhdFile->BlockBitmap, BatEntryIndex + 1, &RunStart) == 0)
                        RunStart = VhdFile->BlockSectors; /* If not found... */
                    /* RunStart points to the next run */
                    RunSize = (RunStart - BatEntryIndex);

                    /* Read the entire region of sectors */
                    ReadOffset.QuadPart = (VhdFile->BlockAllocationTable[BatEntry] + VhdFile->BlockBitmapSize /* Constant part */
                                            + BatEntryIndex) * VHD_SECTOR_SIZE + SectorOffset;
                    BufSize = min(ReadLength, RunSize * VHD_SECTOR_SIZE - SectorOffset);
                    Status = VhdFile->FileRead(VhdFile,
                                               &ReadOffset,
                                               Buffer,
                                               BufSize,
                                               &BufSize);
                    if (!NT_SUCCESS(Status))
                        DPRINT1("FileRead() failed to read next sectors (Status 0x%08lx)\n", Status);
                }
                else
                {
                    /*
                     * Find the size of this run of clear bits, that gives us
                     * the number of continuous clear/invalid sectors.
                     */
                    RunSize = RtlFindNextForwardRunClear(&VhdFile->BlockBitmap, BatEntryIndex, &RunStart);
                    if (RunSize == 0) /* If not found... */
                    {
                        ASSERT(FALSE); // Technically, this should never happen...

                        /* By construction we know that this sector is actually clear */
                        RunStart = BatEntryIndex + 1;
                        RunSize  = (RunStart - BatEntryIndex);
                    }
                    else
                    {
                        /* Go to the next run */
                        RunStart += RunSize;
                    }

                    /* Read the entire region of sectors */
                    BufSize = min(ReadLength, RunSize * VHD_SECTOR_SIZE - SectorOffset);

                    // For dynamic disks only...
                    if (TRUE)
                    {
                        /* Just zero out the memory region */
                        RtlZeroMemory(Buffer, BufSize);
                    }
                    else
                    // For differencing disks only...
                    {
                        /* Read parent disk */
                        // TODO!
                    }
                }

                Buffer = (PVOID)((ULONG_PTR)Buffer + BufSize);
                ReadLength -= BufSize;
                SectorOffset = 0;
            }

            Buffer = OldBuffer;
            ReadLength = OldReadLength;
        }

        Buffer = (PVOID)((ULONG_PTR)Buffer + ReadLength);
        Length -= ReadLength;
        FileOffset.QuadPart += ReadLength;
    }

    return Status;
}


/* WRITING DISK IMAGES ********************************************************/

static NTSTATUS
VhdWriteDiskNone(
    IN PVHDFILE VhdFile,
    IN PVOID Buffer,
    IN ULONG Length, // SIZE_T
    IN PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    return STATUS_SUCCESS;
}

static NTSTATUS
VhdWriteFixedDisk(
    IN PVHDFILE VhdFile,
    IN PVOID Buffer,
    IN ULONG Length, // SIZE_T
    IN PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    NTSTATUS Status;
    // LARGE_INTEGER FileOffset;

    /* Directly write the data */
    Status = VhdFile->FileWrite(VhdFile,
                                ByteOffset,
                                Buffer,
                                Length,
                                &Length);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

    // TODO: Return the number of bytes actually written??

    return Status;
}

static NTSTATUS
VhdWriteDynamicDisk(
    IN PVHDFILE VhdFile,
    IN PVOID Buffer,
    IN ULONG Length, // SIZE_T
    IN PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG BufSize, WriteLength; // SIZE_T
    LARGE_INTEGER WriteOffset, FileOffset;
    ULONG Sector, SectorOffset, BatEntry, BatEntryIndex;
    // ULONG RunStart, RunSize;
    // BOOLEAN IsSectorValid;

    FileOffset = *ByteOffset;

    while (Length > 0)
    {
        /* Get the next data block */
        VhdGetBATEntry(VhdFile, &FileOffset,
                       &Sector, &SectorOffset,
                       &BatEntry, &BatEntryIndex);
        // FIXME: 'Sector' (corresponding to the sector number viewed from the host OS) is unused

        /* Clip the write range to remain in this data block */
        WriteLength = min(Length, (VhdFile->BlockSize - (BatEntryIndex * VHD_SECTOR_SIZE) - SectorOffset));

        /* If the block is not allocated, allocate it */
        if (VhdFile->BlockAllocationTable[BatEntry] == ~0UL)
        {
            ULONG BatEntryValue;

            /* Initialize the bitmap for the new block */
#if 0
            RtlInitializeBitMap(&VhdFile->BlockBitmap,
                                VhdFile->BlockBitmapBuffer,
                                VhdFile->BlockSectors);
#endif
            RtlFillMemory(VhdFile->BlockBitmapBuffer, (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE), 0xFF);
            RtlClearAllBits(&VhdFile->BlockBitmap);
            // RtlSetAllBits(&VhdFile->BlockBitmap);

            /* Append a new block (bitmap + data) at the end of the disk (overwrite the footer) */
            WriteOffset = VhdFile->EndOfData;
            BufSize = (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE); // + VhdFile->BlockSize;
            Status = VhdFile->FileWrite(VhdFile,
                                        &WriteOffset, // &VhdFile->EndOfData,
                                        VhdFile->BlockBitmapBuffer,
                                        BufSize,
                                        &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

            /* Add this block to the BAT and update the BAT entry on disk */
            BatEntryValue = (VhdFile->EndOfData.QuadPart / VHD_SECTOR_SIZE);
            VhdFile->BlockAllocationTable[BatEntry] = BatEntryValue;
            BatEntryValue = HOST_TO_BE32(BatEntryValue);

            WriteOffset.QuadPart = VhdFile->BlockAllocationTableOffset + BatEntry * sizeof(ULONG);
            BufSize = sizeof(BatEntryValue); // == sizeof(ULONG) == sizeof(*VhdFile->BlockAllocationTable);
            Status = VhdFile->FileWrite(VhdFile,
                                        &WriteOffset,
                                        &BatEntryValue,
                                        BufSize,
                                        &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

            /* Advance the end of the disk */
            // VhdFile->EndOfData.QuadPart += BufSize;
            VhdFile->EndOfData.QuadPart += (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE) + VhdFile->BlockSize;
            // ASSERT that this is still VHD_SECTOR_SIZE aligned...

            // TODO: Zero out the expansion (optional...)

            //
            // FIXME: TODO: Update the sizes inside the footer, recompute checksum
            // and update also the corresponding header??
            //
            /* Write an updated footer (extending the disk image) */
            WriteOffset = VhdFile->EndOfData;
            BufSize = sizeof(VhdFile->Footer); // sizeof(VHD_FOOTER);
            Status = VhdFile->FileWrite(VhdFile,
                                        &WriteOffset, // &VhdFile->EndOfData,
                                        &VhdFile->Footer,
                                        BufSize,
                                        &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);
        }
        else
        {
            /* The block is allocated, load the bitmap of this block */
            WriteOffset.QuadPart = (VhdFile->BlockAllocationTable[BatEntry] * VHD_SECTOR_SIZE);
            BufSize = (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE);
            Status = VhdFile->FileRead(VhdFile,
                                       &WriteOffset,
                                       VhdFile->BlockBitmapBuffer,
                                       BufSize,
                                       &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileRead() failed to read block bitmap (Status 0x%08lx)\n", Status);

#if 0
            RtlInitializeBitMap(&VhdFile->BlockBitmap,
                                VhdFile->BlockBitmapBuffer,
                                VhdFile->BlockSectors);
#endif
        }

        /* Write the entire region of sectors */
        WriteOffset.QuadPart = (VhdFile->BlockAllocationTable[BatEntry] + VhdFile->BlockBitmapSize /* Constant part */
                                + BatEntryIndex) * VHD_SECTOR_SIZE + SectorOffset;
        // BufSize = min(WriteLength, RunSize * VHD_SECTOR_SIZE - SectorOffset);
        BufSize = WriteLength;
        Status = VhdFile->FileWrite(VhdFile,
                                    &WriteOffset,
                                    Buffer,
                                    BufSize,
                                    &BufSize);
        if (!NT_SUCCESS(Status))
            DPRINT1("FileWrite() failed to write next sectors (Status 0x%08lx)\n", Status);

        RtlSetBits(&VhdFile->BlockBitmap,
                   BatEntryIndex,
                   (/*VhdFile->BlockSize / VHD_SECTOR_SIZE*/ VhdFile->BlockSectors - BatEntryIndex));

        /* Update the bitmap */
        WriteOffset.QuadPart = (VhdFile->BlockAllocationTable[BatEntry] * VHD_SECTOR_SIZE);
        BufSize = (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE);
        Status = VhdFile->FileWrite(VhdFile,
                                    &WriteOffset,
                                    VhdFile->BlockBitmapBuffer,
                                    BufSize,
                                    &BufSize);
        if (!NT_SUCCESS(Status))
            DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);


        Buffer = (PVOID)((ULONG_PTR)Buffer + WriteLength);
        Length -= WriteLength;
        FileOffset.QuadPart += WriteLength;
    }

    return Status;
}


/* FUNCTION TABLE *************************************************************/

typedef NTSTATUS
(*PVHD_OPEN_DISK)(
    IN PVHDFILE VhdFile);

typedef NTSTATUS
(*PVHD_READ_DISK)(
    IN  PVHDFILE VhdFile,
    OUT PVOID Buffer,
    IN  ULONG Length, // SIZE_T
    IN  PLARGE_INTEGER ByteOffset); // OPTIONAL

typedef NTSTATUS
(*PVHD_WRITE_DISK)(
    IN PVHDFILE VhdFile,
    IN PVOID Buffer,
    IN ULONG Length, // SIZE_T
    IN PLARGE_INTEGER ByteOffset); // OPTIONAL

typedef struct _VHD_TABLE
{
    PVHD_OPEN_DISK  OpenDisk;
    PVHD_READ_DISK  ReadDisk;
    PVHD_WRITE_DISK WriteDisk;
} VHD_TABLE, *PVHD_TABLE;

static VHD_TABLE Table[VHD_MAX_TYPE] =
{
    /* RESERVED_0       */  {VhdOpenDiskNone, VhdReadDiskNone, VhdWriteDiskNone},
    /* RESERVED_1       */  {VhdOpenDiskNone, VhdReadDiskNone, VhdWriteDiskNone},
    /* VHD_FIXED        */  {VhdOpenFixedDisk  , VhdReadFixedDisk  , VhdWriteFixedDisk  },
    /* VHD_DYNAMIC      */  {VhdOpenDynamicDisk, VhdReadDynamicDisk, VhdWriteDynamicDisk},
    /* VHD_DIFFERENCING */  {VhdOpenDiffDisk   , VhdReadDynamicDisk, VhdWriteDynamicDisk},
    /* RESERVED_2       */  {VhdOpenDiskNone, VhdReadDiskNone, VhdWriteDiskNone},
    /* RESERVED_3       */  {VhdOpenDiskNone, VhdReadDiskNone, VhdWriteDiskNone},
};


/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
VhdCreateDisk(
    IN PVHDFILE VhdFile,
    IN VHD_TYPE Type)
{
    // NTSTATUS Status;
    VHD_FOOTER Footer;
    VHD_DYNAMIC_HEADER DynHeader;

    if (Type != VHD_FIXED &&
        Type != VHD_DYNAMIC &&
        Type != VHD_DIFFERENCING /* && Type != RESERVED_3 */)
    {
        DPRINT1("CreateDisk: Unsupported VHD HDD type %d", Type);
        return STATUS_INVALID_PARAMETER;
    }

    // HOST_TO_BE16

    /* Initialize the standard VHD footer */
    Footer;

    /* For dynamic or differencing disks, also initialize the header */
    if (Type == VHD_DYNAMIC ||
        Type == VHD_DIFFERENCING)
    {
        DynHeader;
    }

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
VhdOpenDisk(
    IN OUT PVHDFILE VhdFile,
    IN ULONG    FileSize,
    // IN BOOLEAN  CreateNew,
    IN BOOLEAN  ReadOnly,
    IN PVHD_ALLOCATE_ROUTINE   Allocate,
    IN PVHD_FREE_ROUTINE       Free,
    IN PVHD_FILE_SET_SIZE_ROUTINE FileSetSize,
    IN PVHD_FILE_WRITE_ROUTINE FileWrite,
    IN PVHD_FILE_READ_ROUTINE  FileRead,
    IN PVHD_FILE_FLUSH_ROUTINE FileFlush)
{
    NTSTATUS Status;
    LARGE_INTEGER FileOffset;
    ULONG BytesToRead;
    VHD_FOOTER Footer, Header;
    ULONG checksum;

    ASSERT(VhdFile);

    // /* Creating a new log file with the 'ReadOnly' flag set is incompatible */
    // if (CreateNew && ReadOnly)
        // return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(VhdFile, sizeof(*VhdFile));

    VhdFile->Allocate  = Allocate;
    VhdFile->Free      = Free;
    VhdFile->FileSetSize = FileSetSize;
    VhdFile->FileWrite = FileWrite;
    VhdFile->FileRead  = FileRead;
    VhdFile->FileFlush = FileFlush;

    VhdFile->CurrentSize = FileSize;

    /*
     * Go to the end of the file and read the footer.
     * As the footer might be 511-byte large only (done by versions
     * prior to MS Virtual PC 2004), instead of 512 bytes, check for
     * 511-byte only & rounded down to a VHD sector size.
     */
    VhdFile->EndOfData.QuadPart = ROUND_DOWN(FileSize - sizeof(VHD_FOOTER) + 1, VHD_SECTOR_SIZE);

    FileOffset = VhdFile->EndOfData;
    BytesToRead = sizeof(VHD_FOOTER);
    Status = VhdFile->FileRead(VhdFile,
                               &FileOffset,
                               &Footer,
                               BytesToRead,
                               &BytesToRead);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileRead() failed when reading HDD footer (Status 0x%08lx)\n", Status);
        return Status;
    }

    /* Perform validity checks */
    if (RtlCompareMemory(Footer.creator, "conectix",
                         sizeof(Footer.creator)) != sizeof(Footer.creator))
    {
        DPRINT1("OpenHDD: Invalid HDD image (expected VHD).");
        return FALSE;
    }
    if (BE32_TO_HOST(Footer.version) != 0x00010000 &&
        BE32_TO_HOST(Footer.version) != 0x00050000)
    {
        DPRINT1("OpenHDD: VHD HDD image of unexpected version %d.", Footer.version);
        return FALSE;
    }

    VhdFile->Type = BE32_TO_HOST(Footer.type);
    if (VhdFile->Type != VHD_FIXED &&
        VhdFile->Type != VHD_DYNAMIC &&
        VhdFile->Type != VHD_DIFFERENCING /* && VhdFile->Type != RESERVED_3 */)
    {
        DPRINT1("OpenHDD: Unsupported VHD HDD type %d", VhdFile->Type);
        return FALSE;
    }

    /* Verify checksum */
    checksum = BE32_TO_HOST(Footer.checksum);
    Footer.checksum = 0;
    if (VhdComputeChecksum(&Footer, sizeof(Footer)) != checksum)
    {
        DPRINT1("OpenHDD: VHD HDD checksum does not match.");
        return FALSE;
    }
    Footer.checksum = HOST_TO_BE32(checksum);

    /* Go to the beginning of the file and read the header */
    if (VhdFile->Type == VHD_DYNAMIC || VhdFile->Type == VHD_DIFFERENCING) // VhdFile->Type != VHD_FIXED
    {
        FileOffset.QuadPart = 0LL;
        BytesToRead = sizeof(VHD_FOOTER);
        Status = VhdFile->FileRead(VhdFile,
                                   &FileOffset,
                                   &Header,
                                   BytesToRead,
                                   &BytesToRead);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("FileRead() failed when reading HDD header (Status 0x%08lx)\n", Status);
            return Status;
        }

        /* Header and footer must be the same */
        if (RtlCompareMemory(&Header, &Footer,
                             sizeof(VHD_FOOTER)) != sizeof(VHD_FOOTER))
        {
            DPRINT1("OpenHDD: Invalid HDD image (expected VHD).");
            return FALSE;
        }
    }

    /* Save a copy of the VHD footer */
    RtlCopyMemory(&VhdFile->Footer, &Footer, sizeof(VHD_FOOTER));


#if 0

    /*
     * The visible size of a image in Virtual PC depends on the geometry
     * rather than on the size stored in the footer (the size in the footer
     * is too large usually).
     */
    bs->total_sectors = (int64_t)
        be16_to_cpu(Footer.cyls) * Footer.heads * Footer.secs_per_cyl;

    /*
     * Microsoft Virtual PC and Microsoft Hyper-V produce and read
     * VHD image sizes differently. VPC will rely on CHS geometry,
     * while Hyper-V and disk2vhd use the size specified in the footer.
     *
     * We use a couple of approaches to try and determine the correct method:
     * look at the Creator App field, and look for images that have CHS
     * geometry that is the maximum value.
     *
     * If the CHS geometry is the maximum CHS geometry, then we assume that
     * the size is the footer->current_size to avoid truncation. Otherwise,
     * we follow the table based on footer->creator_app:
     *
     *  Known creator apps:
     *      'vpc '  :  CHS              Virtual PC (uses disk geometry)
     *      'qemu'  :  CHS              QEMU (uses disk geometry)
     *      'qem2'  :  current_size     QEMU (uses current_size)
     *      'win '  :  current_size     Hyper-V
     *      'd2v '  :  current_size     Disk2vhd
     *      'tap\0' :  current_size     XenServer
     *      'CTXS'  :  current_size     XenConverter
     *
     * The user can override the table values via drive options, however
     * even with an override we will still use current_size for images
     * that have CHS geometry of the maximum size.
     */
    use_chs = (!!memcmp(Footer.creator_app, "win " , 4) &&
               !!memcmp(Footer.creator_app, "qem2" , 4) &&
               !!memcmp(Footer.creator_app, "d2v " , 4) &&
               !!memcmp(Footer.creator_app, "CTXS" , 4) &&
               !!memcmp(Footer.creator_app, "tap\0", 4)) || s->force_use_chs;

    if (!use_chs || bs->total_sectors == VHD_MAX_GEOMETRY || s->force_use_sz)
        bs->total_sectors = be64_to_cpu(Footer.current_size) / VHD_SECTOR_SIZE;

#endif

    VhdFile->DiskInfo.Cylinders = BE16_TO_HOST(Footer.cyls);
    VhdFile->DiskInfo.Heads     = Footer.heads;
    VhdFile->DiskInfo.Sectors   = Footer.secs_per_cyl;
    VhdFile->DiskInfo.SectorSize = BE64_TO_HOST(Footer.current_size) /
                                   VhdFile->DiskInfo.Cylinders /
                                   VhdFile->DiskInfo.Heads / VhdFile->DiskInfo.Sectors;

    VhdFile->TotalSize = BE64_TO_HOST(Footer.current_size); // HACK

    /* Update its read/write state */
    VhdFile->ReadOnly = ReadOnly;

    return Table[VhdFile->Type].OpenDisk(VhdFile);
}

NTSTATUS
NTAPI
VhdFlushDisk(
    IN PVHDFILE VhdFile)
{
    NTSTATUS Status;

    ASSERT(VhdFile);

    if (VhdFile->ReadOnly)
        return STATUS_SUCCESS; // STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    // TODO: Flush the Block Allocation Table (in case of a dynamic or differencing image)
    // TODO: Update footer (and header in case of a dynamic or differencing image)

    Status = VhdFile->FileFlush(VhdFile, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileFlush() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    return Status;
}

VOID    // or NTSTATUS, to return whether or not the disk is still used??
NTAPI
VhdCloseDisk(
    IN PVHDFILE VhdFile)
{
    // NTSTATUS Status;

    ASSERT(VhdFile);

    /* Flush the VHD file */
    VhdFlushDisk(VhdFile);

    /* Free the data */
    if (VhdFile->BlockAllocationTable)
        VhdFile->Free(VhdFile->BlockAllocationTable, 0, 0);
    if (VhdFile->BlockBitmapBuffer)
        VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);

    // return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
VhdReadDisk(
    IN  PVHDFILE VhdFile,
    OUT PVOID Buffer,
    IN  ULONG Length, // SIZE_T
    IN  PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    ASSERT(VhdFile);

    if (ByteOffset->QuadPart + Length > VhdFile->TotalSize)
        return STATUS_INVALID_PARAMETER;

    return Table[VhdFile->Type].ReadDisk(VhdFile,
                                         Buffer,
                                         Length,
                                         ByteOffset);
}

NTSTATUS
NTAPI
VhdWriteDisk(
    IN PVHDFILE VhdFile,
    IN PVOID Buffer,
    IN ULONG Length, // SIZE_T
    IN PLARGE_INTEGER ByteOffset) // OPTIONAL
{
    ASSERT(VhdFile);

    if (VhdFile->ReadOnly)
        return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    if (ByteOffset->QuadPart + Length > VhdFile->TotalSize)
        return STATUS_INVALID_PARAMETER;

    return Table[VhdFile->Type].WriteDisk(VhdFile,
                                          Buffer,
                                          Length,
                                          ByteOffset);
}

NTSTATUS
NTAPI
VhdCompactDisk(
    IN PVHDFILE VhdFile)
{
    ASSERT(VhdFile);

    if (VhdFile->ReadOnly)
        return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
VhdExpandDisk(
    IN PVHDFILE VhdFile)
{
    ASSERT(VhdFile);

    if (VhdFile->ReadOnly)
        return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
VhdRepairDisk(
    IN PVHDFILE VhdFile)
{
    ASSERT(VhdFile);

    if (VhdFile->ReadOnly)
        return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
