/*
 * PROJECT:         ReactOS VHD File Library
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            sdk/lib/vhdlib/vhdlib.h
 * PURPOSE:         Virtual Hard Disk Format.
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include "vhdlib.h"
// #include <ndk/rtlfuncs.h>

#define NDEBUG
#include <debug.h>

// STATUS_INVALID_BLOCK_LENGTH
// STATUS_INVALID_BUFFER_SIZE
// STATUS_INSUFFICIENT_RESOURCES
// STATUS_NO_MEMORY


/* HELPER FUNCTIONS ***********************************************************/

/**
0x0000000000000043  0x0000  0x04   0x11		0
0x0000000000000044  0x0001  0x04   0x11		0x44			0x44 == 68 (sectors) ==> 68 * (512 bytes per sector) == 34.816 bytes == 34 kB
.
.
0x00000000000153fe  0x03ff  0x05   0x11		0x153AB
0x00000000000153ff  0x03ff  0x05   0x11
0x0000000000015400  0x00af  0x10   0x1f		0x15310
0x0000000000015401  0x00af  0x10   0x1f
0x0000000000015402  0x00af  0x10   0x1f
.......................................
0x000000000001540e  0x00af  0x10   0x1f
0x000000000001540f  0x00af  0x10   0x1f
0x0000000000015410  0x00af  0x10   0x1f
0x0000000000015411  0x0355  0x06   0x11		0x153DE
.
.
0x00000000000197ff  0x03ff  0x06   0x11		0x1979A
0x0000000000019800  0x00d2  0x10   0x1f		0x196E0
0x0000000000019801  0x00d2  0x10   0x1f
0x0000000000019802  0x00d2  0x10   0x1f
.......................................
0x000000000001980e  0x00d2  0x10   0x1f
0x000000000001980f  0x00d2  0x10   0x1f
0x0000000000019810  0x00d2  0x10   0x1f
0x0000000000019811  0x036d  0x07   0x11		0x197AB
.
.
0x000000000003fbff  0x03ff  0x0f   0x11		0x3FB01
0x000000000003fc00  0x020e  0x10   0x1f		0x3FB20
0x000000000003fc01  0x020e  0x10   0x1f
0x000000000003fc02  0x020e  0x10   0x1f
.......................................
0x000000000003fc0e  0x020e  0x10   0x1f
0x000000000003fc0f  0x020e  0x10   0x1f
0x000000000003fc10  0x020e  0x10   0x1f
0x000000000003fc11  0x03c0  0x10   0x11		0x3FC00
**/
/*
 * Calculates the number of cylinders, heads and sectors per cylinder
 * based on a given number of sectors. This is the algorithm described
 * in the VHD specification.
 *
 * Note that the geometry doesn't always exactly match TotalSize
 * (i.e. TotalSectors) but may round it down.
 */
static VOID
CalculateCHSGeometry(
    UINT64 TotalSize, PUINT16 Cylinders,
    PUINT8 Heads, PUINT8 SectorsPerTrack)   // Heads == NumberOfHeads == NumberOfTracks == TracksPerCylinder
{
    UINT64 TotalSectors = TotalSize / VHD_SECTOR_SIZE;
    UINT32 CylsTimesHeads;

    /* ATA disks are limited to 127 GB */
    // VHD_MAX_GEOMETRY == 65535 * 16 * 255
    TotalSectors = min(TotalSectors, VHD_MAX_GEOMETRY);

    if (TotalSectors > 65535 * 16 * 63)
    {
        *SectorsPerTrack = 255;
        *Heads = 16;
        CylsTimesHeads = (UINT32)(TotalSectors / *SectorsPerTrack);
    }
    else
    {
        *SectorsPerTrack = 17;
        CylsTimesHeads = (UINT32)(TotalSectors / *SectorsPerTrack);

        *Heads = (CylsTimesHeads + 1023) / 1024;
        if (*Heads < 4)
            *Heads = 4;

        if (CylsTimesHeads >= (*Heads * 1024) || *Heads > 16)
        {
            *SectorsPerTrack = 31;
            *Heads = 16;
            CylsTimesHeads = (UINT32)(TotalSectors / *SectorsPerTrack);
        }

        if (CylsTimesHeads >= (*Heads * 1024))
        {
            *SectorsPerTrack = 63;
            *Heads = 16;
            CylsTimesHeads = (UINT32)(TotalSectors / *SectorsPerTrack);
        }
    }

    *Cylinders = (UINT16)(CylsTimesHeads / *Heads);
}

/*
 * Checksum-32 algorithm
 *
 * NOTES:
 *   -x == ~x + 1
 *   ~(a+b) == ~a + ~b + 1
 * thus:
 *   ~(a-b) == ~a + b (because ~1+2 == 0)
 *
 * With 'Block' = union('Data', 'Checksum(Data)'), we have:
 *   Checksum(Data) = Checksum(Block) + ~Checksum(Checksum(Data))
 *                  = ~Sum(Block) + ~Checksum(Checksum(Data)) + 1 - 1
 *                  = ~(Sum(Block) + Checksum(Checksum(Data))) - 1 , and -1 == ~1 + 1
 *                  = ~(Sum(Block) + Checksum(Checksum(Data)) + 1)
 *                  = ~(Sum(Block) + ~Sum(Checksum(Data)) + 1) .
 * --> Initial checksum value == Checksum(Checksum(Data)) + 1 == ~(~Checksum(Checksum(Data))) + 1
 * --> StartValue == ~Checksum(Checksum(Data))
 *
 * But also,
 *   Checksum(Checksum(Data)) = ~Sum(Checksum(Data)) ,
 * with initial checksum value == 0 --> StartValue == 0 (because ~0 + 1 == 0)
 */
static ULONG
VhdComputeChecksum(
    IN ULONG  StartValue OPTIONAL,
    IN PVOID  Buffer,
    IN SIZE_T BufSize)
{
    ULONG checksum = ~StartValue+1; // == -StartValue

    while (BufSize-- > 0)
    {
        checksum += (ULONG)*(PUCHAR)Buffer;
        Buffer = (PVOID)((PUCHAR)Buffer + 1);
    }

    return ~checksum;
}

static BOOLEAN
VhdVerifyFooter(
    IN PVHD_FOOTER Footer)
{
    VHD_TYPE VhdType;
    ULONG checksum;

    /* Check for a valid signature */
    if (RtlCompareMemory(Footer->creator, "conectix",
                         sizeof(Footer->creator)) != sizeof(Footer->creator))
    {
        DPRINT1("OpenHDD: Invalid HDD image (expected VHD).");
        return FALSE;
    }

    /* Check for a valid version */
    if (BE32_TO_HOST(Footer->version) != 0x00010000 &&
        BE32_TO_HOST(Footer->version) != 0x00050000)
    {
        DPRINT1("OpenHDD: VHD HDD image of unexpected version %d.", Footer->version);
        return FALSE;
    }

    /* Check for a valid VHD type */
    VhdType = BE32_TO_HOST(Footer->type);
    if (VhdType != VHD_FIXED &&
        VhdType != VHD_DYNAMIC &&
        VhdType != VHD_DIFFERENCING /* && VhdType != RESERVED_3 */)
    {
        DPRINT1("OpenHDD: Unsupported VHD HDD type %d", VhdType);
        return FALSE;
    }

    /* Verify the footer checksum */
    checksum = BE32_TO_HOST(Footer->checksum);
    if (VhdComputeChecksum(~VhdComputeChecksum(0, &checksum, sizeof(checksum)),
                           Footer, sizeof(*Footer)) != checksum)
    {
        DPRINT1("OpenHDD: VHD HDD checksum does not match.");
        return FALSE;
    }

    /* We are good */
    return TRUE;
}

/*static*/
FORCEINLINE VOID
VhdGetBATEntry(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset,
    OUT PULONG Sector,
    OUT PULONG BatEntry,
    OUT PULONG BatEntryIndex)
{
    UINT32 VhdSector;

    /* We must have aligned byte offsets */
    ASSERT(ByteOffset->QuadPart % VHD_SECTOR_SIZE == 0);

    VhdSector = ByteOffset->QuadPart / VHD_SECTOR_SIZE;
    *Sector = VhdSector;
    *BatEntry = VhdSector / VhdFile->BlockSectors;      // BAT index for a data block
    *BatEntryIndex = VhdSector % VhdFile->BlockSectors; // Index for a sector inside the data block

    // FIXME TODO ? Check that *BatEntry is not bigger than VhdFile->BlockAllocationTableEntries ?

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
    PVHD_FOOTER Footer = &VhdFile->Footer;

    if (Footer->data_offset != ~0ULL)
    {
        DPRINT1("OpenHDD: Unexpected data offset for VHD HDD fixed image.");
        return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
    }
    if (Footer->orig_size != Footer->current_size)
    {
        DPRINT1("OpenHDD: VHD HDD fixed image size should be the same as its original size.");
        return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
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
        return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
    }
    if (BE32_TO_HOST(DynHeader.version) != 0x00010000)
    {
        DPRINT1("OpenHDD: VHD HDD dynamic image of unexpected version %d.", DynHeader.version);
        return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
    }

    /* Verify checksum */
    checksum = BE32_TO_HOST(DynHeader.checksum);
    if (VhdComputeChecksum(~VhdComputeChecksum(0, &checksum, sizeof(checksum)),
                           &DynHeader, sizeof(DynHeader)) != checksum)
    {
        DPRINT1("OpenHDD: VHD HDD dynamic header checksum does not match.");
        return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
    }

    if (DynHeader.data_offset != ~0ULL)
    {
        DPRINT1("OpenHDD: Unexpected data offset for VHD HDD dynamic image.");
        return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
    }

    if (DynHeader.reserved != 0)
        DPRINT1("OpenHDD: Reserved field for VHD HDD dynamic image is not zero; continuing...");

    /* Retrieve the data block size information */
    VhdFile->BlockSize = BE32_TO_HOST(DynHeader.block_size); // TODO: Check whether it's a power of 2??
    VhdFile->BlockSectors = VhdFile->BlockSize / VHD_SECTOR_SIZE; // Round down

    VhdFile->CurrentBlockNumber = ~0UL;
    /* Compute the size (sector-aligned) of the block bitmaps (same for all the blocks) */
    VhdFile->BlockBitmapSize = ((VhdFile->BlockSectors / 8) + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE; // ROUND_UP; in number of sectors

    /* Allocate the cache block bitmap (size multiple of sizeof(ULONG), OK for RTL Bitmap API) */
    VhdFile->BlockBitmapBuffer =
        VhdFile->Allocate(VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE,
                          HEAP_ZERO_MEMORY, 0);
    if (!VhdFile->BlockBitmapBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlInitializeBitMap(&VhdFile->BlockBitmap,
                        VhdFile->BlockBitmapBuffer,
                        VhdFile->BlockSectors);
    // RtlClearAllBits(&VhdFile->BlockBitmap);

    /* Allocate & cache the Block Allocation Table */
    VhdFile->BlockAllocationTableEntries = BE32_TO_HOST(DynHeader.max_table_entries);
    // This should be equal to the number of blocks in the disk (that is, the disk size divided by the block size).

    VhdFile->BlockAllocationTableSize = (VhdFile->BlockAllocationTableEntries * sizeof(ULONG) + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE; // ROUND_UP; in number of sectors
    VhdFile->BlockAllocationTable =
        VhdFile->Allocate(VhdFile->BlockAllocationTableSize * VHD_SECTOR_SIZE,
                          HEAP_ZERO_MEMORY, 0);
    if (!VhdFile->BlockAllocationTable)
    {
        VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Read the Block Allocation Table */
    VhdFile->BlockAllocationTableOffset = BE64_TO_HOST(DynHeader.table_offset);
    FileOffset.QuadPart = VhdFile->BlockAllocationTableOffset;
    BytesToRead = VhdFile->BlockAllocationTableSize * VHD_SECTOR_SIZE;
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
// parent_uuid
// parent_timestamp
// parent_name
// parse parent_locator array

    UNIMPLEMENTED;

    return STATUS_NOT_IMPLEMENTED;
}


/* READING DISK IMAGES ********************************************************/

static NTSTATUS
VhdReadDiskNone(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    UNREFERENCED_PARAMETER(VhdFile);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(Buffer);

    /*
     * The caller will determine whether (s)he needs to read from
     * a parent disk (in case this is a differencing disk), or needs
     * to simply zero out the memory region (in case this is just a
     * dynamic disk).
     */
    if (ReadLength) *ReadLength = Length;
    return STATUS_NONEXISTENT_SECTOR;
}

static NTSTATUS
VhdReadFixedDisk(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    NTSTATUS Status;

    if (ReadLength) *ReadLength = 0;

    /* Directly read the data */
    Status = VhdFile->FileRead(VhdFile,
                               ByteOffset,
                               Buffer,
                               Length,
                               ReadLength);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileRead() failed (Status 0x%08lx)\n", Status);

    return Status;
}

/*static*/
FORCEINLINE
NTSTATUS
VhdGetBlockBitmap(
    IN  PVHDFILE VhdFile,
    IN  ULONG BlockSector,
    OUT PRTL_BITMAP* Bitmap)
{
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T ReadLength;
    LARGE_INTEGER ReadOffset;

    /* If the block is not allocated the content of the entry is ~0 */
    if (BlockSector == ~0UL)
    {
        /* No bitmap */
        *Bitmap = NULL;
        VhdFile->CurrentBlockNumber = BlockSector;
    }
    else
    {
        /* Load the bitmap of this block, if not cached */
        if (BlockSector != VhdFile->CurrentBlockNumber)
        {
            ReadOffset.QuadPart = (BlockSector * VHD_SECTOR_SIZE);
            ReadLength = (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE);
            Status = VhdFile->FileRead(VhdFile,
                                       &ReadOffset,
                                       VhdFile->BlockBitmapBuffer,
                                       ReadLength,
                                       &ReadLength);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileRead() failed to read block bitmap (Status 0x%08lx)\n", Status);

#if 0
            RtlInitializeBitMap(&VhdFile->BlockBitmap,
                                VhdFile->BlockBitmapBuffer,
                                VhdFile->BlockSectors);
#endif
        }
        *Bitmap = &VhdFile->BlockBitmap;
        VhdFile->CurrentBlockNumber = BlockSector;
    }

    return Status;
}

/*static*/
FORCEINLINE
NTSTATUS
VhdReadNextBlock(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T BufSize;
    LARGE_INTEGER ReadOffset, FileOffset;
    ULONG Sector, BatEntry, BatEntryIndex, BlockSector;
    ULONG RunStart, RunSize;
    PRTL_BITMAP Bitmap;

    if (ReadLength) *ReadLength = 0;

    FileOffset = *ByteOffset;

    /* Get the next data block */
    VhdGetBATEntry(VhdFile, &FileOffset,
                   &Sector, &BatEntry, &BatEntryIndex);
    // FIXME: 'Sector' (corresponding to the sector number viewed from the host OS) is unused

    // FIXME TODO ? Check that *BatEntry is not bigger than VhdFile->BlockAllocationTableEntries ?
    BlockSector = VhdFile->BlockAllocationTable[BatEntry];

    Status = VhdGetBlockBitmap(VhdFile, BlockSector, &Bitmap);
    if (!NT_SUCCESS(Status))
        DPRINT1("VhdGetBlockBitmap() failed to read block bitmap (Status 0x%08lx)\n", Status);

    Status = STATUS_SUCCESS;

    /* Clip the read range to remain in this data block */
    Length = min(Length, VhdFile->BlockSize - (BatEntryIndex * VHD_SECTOR_SIZE));

    /* If the block is not allocated the content of the entry is ~0 */
    if (!Bitmap)
    {
        /*
         * The caller will determine whether (s)he needs to read from
         * a parent disk (in case this is a differencing disk), or needs
         * to simply zero out the memory region (in case this is just a
         * dynamic disk).
         */
        if (ReadLength) *ReadLength = Length;
        Status = STATUS_NONEXISTENT_SECTOR;
    }
    else
    {
        /* Perform the read proper */

        /*
         * Loop over each sector in the data block's bitmap:
         * - If 0, the sector is empty (dynamic disk), or the data should
         *   be read from a parent disk (differencing disk).
         * - If 1, the sector contains data (dynamic disk), or the data
         *   should be read from the differencing disk.
         *
         * Sector == Index in the sector bitmap.
         * Bitmap->SizeOfBitMap == VhdFile->BlockSectors.
         */

        /* Advance 'BlockSector' to the start of the data in the block */
        BlockSector += VhdFile->BlockBitmapSize;

#if 0
        /*
         * Read in one go the full data block, including any possible
         * sectors marked as invalid (they exist in the file anyway,
         * just their content is meaningless). Then, patch the read buffer
         * with correct data in the places of the invalid sectors.
         *
         * Well, we do this only if the data block contains at least
         * one valid sector...
         */
        if (RtlNumberOfSetBits(Bitmap) > 0)
        {
            /* Read the entire region of sectors */
            ReadOffset.QuadPart = (BlockSector + BatEntryIndex) * VHD_SECTOR_SIZE;
            // BufSize = min(Length, RunSize * VHD_SECTOR_SIZE);
            BufSize = min(Length, Bitmap->SizeOfBitMap /* VhdFile->BlockSectors */ * VHD_SECTOR_SIZE);
            Status = VhdFile->FileRead(VhdFile,
                                       &ReadOffset,
                                       Buffer,
                                       BufSize,
                                       &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileRead() failed to read next sectors (Status 0x%08lx)\n", Status);
        }

        // Now, defer to the caller (VhdReadDynamicDisk) the job of
        // reading the sectors of the parent disks, or zeroing out
        // the regions in the buffer...
        // But then this means the caller should parse the data block bitmap??

        /* Read the entire region of sectors */
        BufSize = min(Length, RunSize * VHD_SECTOR_SIZE);

        if (ReadLength) *ReadLength = BufSize;
        Status = STATUS_NONEXISTENT_SECTOR;
#endif

        // for (RunStart = BatEntryIndex;
             // (Length > 0) && (BatEntryIndex < Bitmap->SizeOfBitMap);
             // BatEntryIndex = RunStart)

        RunStart = BatEntryIndex;

        /* Check if the sector is valid */
        if (!!RtlCheckBit(Bitmap, BatEntryIndex)) // Inline version of RtlTestBit
        {
            /*
             * Find the first next clear bit. The run of set bits then
             * starts with the current sector and ends at this clear bit.
             */
            if (RtlFindNextForwardRunClear(Bitmap, BatEntryIndex + 1, &RunStart) == 0)
                RunStart = Bitmap->SizeOfBitMap; /* If not found... */
            /* RunStart points to the next run */
            RunSize = (RunStart - BatEntryIndex);

            /* Read the entire region of sectors */
            ReadOffset.QuadPart = (BlockSector + BatEntryIndex) * VHD_SECTOR_SIZE;
            BufSize = min(Length, RunSize * VHD_SECTOR_SIZE);
            Status = VhdFile->FileRead(VhdFile,
                                       &ReadOffset,
                                       Buffer,
                                       BufSize,
                                       &BufSize);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileRead() failed to read next sectors (Status 0x%08lx)\n", Status);

            if (ReadLength) *ReadLength = BufSize;
        }
        else
        {
            /*
             * Find the size of this run of clear bits, that gives us
             * the number of continuous clear/invalid sectors.
             */
            RunSize = RtlFindNextForwardRunClear(Bitmap, BatEntryIndex, &RunStart);
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
            BufSize = min(Length, RunSize * VHD_SECTOR_SIZE);

            if (ReadLength) *ReadLength = BufSize;
            Status = STATUS_NONEXISTENT_SECTOR;
        }

        // Buffer  = (PVOID)((ULONG_PTR)Buffer + BufSize);
        // Length -= BufSize;
    }

    return Status;
}

static NTSTATUS
VhdReadDynamicDisk(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL  // Aligned  // DECLSPEC_ALIGN(VHD_SECTOR_SIZE)
    OUT PVOID   Buffer,                         // Aligned  // DECLSPEC_ALIGN(VHD_SECTOR_SIZE)
    IN  SIZE_T  Length,                         // Aligned  // DECLSPEC_ALIGN(VHD_SECTOR_SIZE)
    OUT PSIZE_T ReadLength OPTIONAL)
{
    LARGE_INTEGER FileOffset = *ByteOffset;
    return VhdReadNextBlock(VhdFile, &FileOffset, Buffer, Length, ReadLength);
}


/* WRITING DISK IMAGES ********************************************************/

static NTSTATUS
VhdWriteDiskNone(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    UNREFERENCED_PARAMETER(VhdFile);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(Buffer);

    if (WrittenLength) *WrittenLength = Length;

    /* Do nothing */
    return STATUS_SUCCESS;
}

static NTSTATUS
VhdWriteFixedDisk(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    NTSTATUS Status;

    if (WrittenLength) *WrittenLength = 0;

    /* Directly write the data */
    Status = VhdFile->FileWrite(VhdFile,
                                ByteOffset,
                                Buffer,
                                Length,
                                WrittenLength);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

    return Status;
}

/*static*/
FORCEINLINE
NTSTATUS
VhdWriteNextBlock(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T BufSize;
    LARGE_INTEGER WriteOffset, FileOffset;
    ULONG Sector, BatEntry, BatEntryIndex, BlockSector;
    PRTL_BITMAP Bitmap;

    if (WrittenLength) *WrittenLength = 0;

    FileOffset = *ByteOffset;

    /* Get the next data block */
    VhdGetBATEntry(VhdFile, &FileOffset,
                   &Sector, &BatEntry, &BatEntryIndex);
    // FIXME: 'Sector' (corresponding to the sector number viewed from the host OS) is unused

    // FIXME TODO ? Check that *BatEntry is not bigger than VhdFile->BlockAllocationTableEntries ?
    BlockSector = VhdFile->BlockAllocationTable[BatEntry];

    /* Clip the write range to remain in this data block */
    Length = min(Length, VhdFile->BlockSize - (BatEntryIndex * VHD_SECTOR_SIZE));

    // Status = VhdGetBlockBitmap(VhdFile, BlockSector, &Bitmap);
    // if (!NT_SUCCESS(Status))
        // DPRINT1("VhdGetBlockBitmap() failed to read block bitmap (Status 0x%08lx)\n", Status);

    /* If the block is not allocated, allocate it */
    if (BlockSector == ~0UL)
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

        Bitmap = &VhdFile->BlockBitmap;

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
        BlockSector = VhdFile->BlockAllocationTable[BatEntry] = BatEntryValue;
        BatEntryValue = HOST_TO_BE32(BatEntryValue);

        VhdFile->CurrentBlockNumber = BlockSector;

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
        /* The block is already allocated, load the bitmap of this block */
        Status = VhdGetBlockBitmap(VhdFile, BlockSector, &Bitmap);
        if (!NT_SUCCESS(Status))
            DPRINT1("VhdGetBlockBitmap() failed to read block bitmap (Status 0x%08lx)\n", Status);
    }

    Status = STATUS_SUCCESS;

    /* Update the bitmap */
    RtlSetBits(Bitmap, BatEntryIndex,
               (/*VhdFile->BlockSize / VHD_SECTOR_SIZE*/ VhdFile->BlockSectors - BatEntryIndex));

    WriteOffset.QuadPart = (BlockSector * VHD_SECTOR_SIZE);
    BufSize = (VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE);
    Status = VhdFile->FileWrite(VhdFile,
                                &WriteOffset,
                                VhdFile->BlockBitmapBuffer,
                                BufSize,
                                &BufSize);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

    /* Write the entire region of sectors */
    WriteOffset.QuadPart = (BlockSector + VhdFile->BlockBitmapSize /* Constant part */
                            + BatEntryIndex) * VHD_SECTOR_SIZE;
    // BufSize = min(Length, RunSize * VHD_SECTOR_SIZE);
    BufSize = Length;
    Status = VhdFile->FileWrite(VhdFile,
                                &WriteOffset,
                                Buffer,
                                BufSize,
                                &BufSize);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileWrite() failed to write next sectors (Status 0x%08lx)\n", Status);

    if (WrittenLength) *WrittenLength = BufSize;
    return Status;
}

static NTSTATUS
VhdWriteDynamicDisk(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL  // Aligned  // DECLSPEC_ALIGN(VHD_SECTOR_SIZE)
    IN  PVOID   Buffer,                         // Aligned  // DECLSPEC_ALIGN(VHD_SECTOR_SIZE)
    IN  SIZE_T  Length,                         // Aligned  // DECLSPEC_ALIGN(VHD_SECTOR_SIZE)
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    LARGE_INTEGER FileOffset = *ByteOffset;
    return VhdWriteNextBlock(VhdFile, &FileOffset, Buffer, Length, WrittenLength);
}


/* FUNCTION TABLE *************************************************************/

typedef NTSTATUS
(*PVHD_OPEN_DISK)(
    IN PVHDFILE VhdFile);

typedef NTSTATUS
(*PVHD_READ_DISK)(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL);

typedef NTSTATUS
(*PVHD_WRITE_DISK)(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL);

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

static NTSTATUS
VhdpCreateDynamicDisk(
    IN OUT PVHDFILE VhdFile,
    IN PGUID ParentIdentifier OPTIONAL,
    IN ULONG ParentTimeStamp OPTIONAL,
    IN PCWSTR ParentPath OPTIONAL,
    IN PVHD_PARENT_LOCATOR_INFO ParentLocatorInfoArray OPTIONAL,
    IN ULONG NumberOfParentLocators OPTIONAL)
{
    NTSTATUS Status;
    VHD_DYNAMIC_HEADER DynHeader;
    LARGE_INTEGER FileOffset;
    ULONG BytesToWrite;

    /*
     * Initialize the dynamic image.
     *
     * The overall structure of the dynamic disk is:
     *
     * [Copy of hard disk footer (512 bytes)]
     * [Dynamic disk header (1024 bytes)]
     * [BAT (Block Allocation Table)]
     * [Parent Locators (for differencing disks only)]
     * [Data block 1]
     * [Data block 2]
     * ...
     * [Data block N]
     * [Hard disk footer (512 bytes)]
     */

    VhdFile->EndOfData.QuadPart = 0ULL;

    /* Write an updated "footer" header */
    // FileOffset.QuadPart = 0LL;
    FileOffset = VhdFile->EndOfData;
    BytesToWrite = sizeof(VhdFile->Footer); // sizeof(VHD_FOOTER);
    Status = VhdFile->FileWrite(VhdFile,
                                &FileOffset, // &VhdFile->EndOfData,
                                &VhdFile->Footer,
                                BytesToWrite,
                                &BytesToWrite);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

    VhdFile->EndOfData.QuadPart += BytesToWrite; // sizeof(VhdFile->Footer); // sizeof(VHD_FOOTER);




    /* Initialize the dynamic VHD header */
    /**/RtlFillMemory(&DynHeader, sizeof(DynHeader), 0xFF);/**/

    RtlCopyMemory(DynHeader.magic, "cxsparse", sizeof(DynHeader.magic));
    DynHeader.data_offset = ~0ULL; // So far, there is no "next" data after this header
    DynHeader.table_offset = HOST_TO_BE64(sizeof(VhdFile->Footer) + sizeof(DynHeader));
    DynHeader.version = HOST_TO_BE32(0x00010000);
    DynHeader.max_table_entries = HOST_TO_BE32(VhdFile->BlockAllocationTableEntries);
    DynHeader.block_size = HOST_TO_BE32(VhdFile->BlockSize);

    /* First, initialize the parent disk information to default values */
    RtlZeroMemory(DynHeader.parent_uuid, sizeof(DynHeader.parent_uuid));
    DynHeader.parent_timestamp = 0;
    RtlZeroMemory(DynHeader.parent_name, sizeof(DynHeader.parent_name));
    RtlZeroMemory(DynHeader.parent_locator, sizeof(DynHeader.parent_locator));

    /* Now, initialize the parent disk information with valid values for differencing disks only */
    if (VhdFile->Type == VHD_DIFFERENCING)
    {
        if (ParentIdentifier)
            RtlCopyMemory(DynHeader.parent_uuid, ParentIdentifier, sizeof(DynHeader.parent_uuid));

        DynHeader.parent_timestamp = HOST_TO_BE32(ParentTimeStamp);

        if (ParentPath)
            RtlCopyMemory(DynHeader.parent_name, ParentPath, sizeof(DynHeader.parent_name));

        /* Set up the parent locator information */
        if (ParentLocatorInfoArray && NumberOfParentLocators > 0)
        {
            ULONG i;

            NumberOfParentLocators = min(NumberOfParentLocators, RTL_NUMBER_OF(DynHeader.parent_locator));
            for (i = 0; i < NumberOfParentLocators; ++i)
            {
                DynHeader.parent_locator[i].platform    = ParentLocatorInfoArray[i].Platform;
                DynHeader.parent_locator[i].data_space  = HOST_TO_BE32((ParentLocatorInfoArray[i].DataLength + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE); // ROUND_UP
                DynHeader.parent_locator[i].data_length = HOST_TO_BE32(ParentLocatorInfoArray[i].DataLength);
                DynHeader.parent_locator[i].reserved    = 0;
                DynHeader.parent_locator[i].data_offset = HOST_TO_BE64(0); // FIXME! Start after the BAT
            }
        }
    }

    DynHeader.reserved = 0;

    RtlZeroMemory(&DynHeader.Padding, sizeof(DynHeader.Padding));

    /* Compute the dynamic header checksum */
    DynHeader.checksum = 0;
    DynHeader.checksum = HOST_TO_BE32(VhdComputeChecksum(0, &DynHeader, sizeof(DynHeader)));


    FileOffset = VhdFile->EndOfData;
    /**** FIXME: 512-byte alignment!!!!!!!! ****/
    BytesToWrite = sizeof(DynHeader); // sizeof(VHD_DYNAMIC_HEADER);
    Status = VhdFile->FileWrite(VhdFile,
                                &FileOffset, // &VhdFile->EndOfData,
                                &DynHeader,
                                BytesToWrite,
                                &BytesToWrite);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);
        // VhdFile->Free(VhdFile->BlockAllocationTable, 0, 0);
        // // VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);
        // return Status;
    }

    VhdFile->EndOfData.QuadPart += BytesToWrite; // sizeof(DynHeader); // sizeof(VHD_DYNAMIC_HEADER);


    // FIXME TODO: BAT !!

    // VhdFile->BlockAllocationTableOffset = BE64_TO_HOST(DynHeader.table_offset);
    // FileOffset.QuadPart = VhdFile->BlockAllocationTableOffset;
    FileOffset = VhdFile->EndOfData;
    BytesToWrite = VhdFile->BlockAllocationTableSize * VHD_SECTOR_SIZE;
    Status = VhdFile->FileWrite(VhdFile,
                                &FileOffset, // &VhdFile->EndOfData,
                                VhdFile->BlockAllocationTable,
                                BytesToWrite,
                                &BytesToWrite);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);
        // VhdFile->Free(VhdFile->BlockAllocationTable, 0, 0);
        // // VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);
        // return Status;
    }

    VhdFile->EndOfData.QuadPart += BytesToWrite; // VhdFile->BlockAllocationTableEntries * sizeof(ULONG);

    // FIXME TODO: Parent locators !!

    return Status;
}

NTSTATUS
NTAPI
VhdCreateDisk(
    IN OUT PVHDFILE VhdFile,
    IN VHD_TYPE Type,
    IN BOOLEAN  WipeOut,
    IN UINT64   TotalSize, // ULONGLONG MaximumSize
    IN ULONG    BlockSizeInBytes,  // UINT32
    IN ULONG    SectorSizeInBytes, // UINT32 // I think it's useless, because for VHDs it can only be == 512 bytes == VHD_SECTOR_SIZE
    IN PGUID    UniqueId OPTIONAL,
    IN ULONG    TimeStamp,
    IN PGUID ParentIdentifier OPTIONAL,
    IN ULONG ParentTimeStamp OPTIONAL,
    IN PCWSTR ParentPath OPTIONAL,
    IN PVHD_PARENT_LOCATOR_INFO ParentLocatorInfoArray OPTIONAL,
    IN ULONG NumberOfParentLocators OPTIONAL,
    IN CHAR  CreatorApp[4],
    IN ULONG CreatorVersion,
    IN CHAR  CreatorHostOS[4],
    IN PVHD_ALLOCATE_ROUTINE   Allocate,
    IN PVHD_FREE_ROUTINE       Free,
    IN PVHD_FILE_SET_SIZE_ROUTINE FileSetSize,
    IN PVHD_FILE_WRITE_ROUTINE FileWrite,
    IN PVHD_FILE_READ_ROUTINE  FileRead,
    IN PVHD_FILE_FLUSH_ROUTINE FileFlush)
{
    NTSTATUS Status;
    PVHD_FOOTER Footer = &VhdFile->Footer;
    LARGE_INTEGER FileOffset;
    ULONG BytesToWrite;

    ASSERT(VhdFile);

    /* Check for a valid VHD type */
    if (Type != VHD_FIXED &&
        Type != VHD_DYNAMIC &&
        Type != VHD_DIFFERENCING /* && Type != RESERVED_3 */)
    {
        DPRINT1("CreateDisk: Unsupported VHD HDD type %d", Type);
        return STATUS_INVALID_PARAMETER;
    }

    if (SectorSizeInBytes != VHD_SECTOR_SIZE)
    {
        DPRINT1("CreateDisk: Invalid VHD sector size %d, expecting %d",
                SectorSizeInBytes, VHD_SECTOR_SIZE);
        return STATUS_INVALID_PARAMETER;
    }

    // TODO: Add a check for a valid minimal & maximal size for TotalSize !!

    RtlZeroMemory(VhdFile, sizeof(*VhdFile));

    VhdFile->Allocate  = Allocate;
    VhdFile->Free      = Free;
    VhdFile->FileSetSize = FileSetSize;
    VhdFile->FileWrite = FileWrite;
    VhdFile->FileRead  = FileRead;
    VhdFile->FileFlush = FileFlush;

    VhdFile->Type = Type; // VhdType;
    VhdFile->TotalSize = TotalSize;

    /* Derive a CHS geometry from the disk image size */
    /*** PhysicalSectorSize ; reported by VHD (ie. virtual) ***/
    CalculateCHSGeometry(TotalSize,
                         &VhdFile->DiskInfo.Cylinders,  // UINT16
                         &VhdFile->DiskInfo.Heads,      // UINT8
                         &VhdFile->DiskInfo.Sectors);   // UINT8

    VhdFile->DiskInfo.SectorSize = TotalSize /
                                   VhdFile->DiskInfo.Cylinders /
                                   VhdFile->DiskInfo.Heads / VhdFile->DiskInfo.Sectors;

    /* For dynamic or differencing disks, also initialize the header */
    if (Type == VHD_DYNAMIC || Type == VHD_DIFFERENCING)
    {
        /* Retrieve the data block size information */
        VhdFile->BlockSize = BlockSizeInBytes;
        VhdFile->BlockSectors = VhdFile->BlockSize / VHD_SECTOR_SIZE; // Round down

        VhdFile->CurrentBlockNumber = ~0UL;
        /* Compute the size (sector-aligned) of the block bitmaps (same for all the blocks) */
        VhdFile->BlockBitmapSize = ((VhdFile->BlockSectors / 8) + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE; // ROUND_UP; in number of sectors

        /* Allocate the cache block bitmap (size multiple of sizeof(ULONG), OK for RTL Bitmap API) */
        VhdFile->BlockBitmapBuffer =
            VhdFile->Allocate(VhdFile->BlockBitmapSize * VHD_SECTOR_SIZE,
                              HEAP_ZERO_MEMORY, 0);
        if (!VhdFile->BlockBitmapBuffer)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlInitializeBitMap(&VhdFile->BlockBitmap,
                            VhdFile->BlockBitmapBuffer,
                            VhdFile->BlockSectors);
        // RtlClearAllBits(&VhdFile->BlockBitmap);

        /* Allocate & cache the Block Allocation Table */
        /** VhdFile->BlockAllocationTableEntries = (TotalSize + VhdFile->BlockSize - 1) / VhdFile->BlockSize; // ROUND_UP **/
        VhdFile->BlockAllocationTableEntries = ROUND_UP(TotalSize, (UINT64)VhdFile->BlockSize) / VhdFile->BlockSize;
        // This should be equal to the number of blocks in the disk (that is, the disk size divided by the block size).

        VhdFile->BlockAllocationTableSize = (VhdFile->BlockAllocationTableEntries * sizeof(ULONG) + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE; // ROUND_UP; in number of sectors
        VhdFile->BlockAllocationTable =
            VhdFile->Allocate(VhdFile->BlockAllocationTableSize * VHD_SECTOR_SIZE,
                              HEAP_ZERO_MEMORY, 0);
        if (!VhdFile->BlockAllocationTable)
        {
            VhdFile->Free(VhdFile->BlockBitmapBuffer, 0, 0);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Initialize the Block Allocation Table with empty values ~0UL */
#if 0
        for (i = 0; i < VhdFile->BlockAllocationTableEntries; ++i)
            VhdFile->BlockAllocationTable[i] = ~0UL;
#else
        RtlFillMemoryUlong(VhdFile->BlockAllocationTable,
                           // VhdFile->BlockAllocationTableEntries * sizeof(ULONG),
                           VhdFile->BlockAllocationTableSize * VHD_SECTOR_SIZE,
                           ~0UL);
#endif
    }





    /* Initialize the standard VHD footer */
    /**/RtlFillMemory(Footer, sizeof(*Footer), 0xFF);/**/

    RtlCopyMemory(Footer->creator, "conectix", sizeof(Footer->creator));
    Footer->version  = HOST_TO_BE32(0x00010000); // or 0x00050000
    Footer->features = HOST_TO_BE32(0x00 | 0x02); // 0x02 == Reserved (always set);
                                                  // 0x01 == Temporary ; 0x00 == No features enabled

    Footer->timestamp = HOST_TO_BE32(TimeStamp);
    RtlCopyMemory(Footer->creator_app, CreatorApp, sizeof(Footer->creator_app));
    Footer->creator_version = HOST_TO_BE32(CreatorVersion);
    RtlCopyMemory(Footer->creator_os, CreatorHostOS, sizeof(Footer->creator_os));

    if (UniqueId)
        RtlCopyMemory(Footer->uuid, UniqueId, sizeof(Footer->uuid));
    else
        RtlZeroMemory(Footer->uuid, sizeof(Footer->uuid));

    Footer->type = HOST_TO_BE32(VhdFile->Type);

    /* Set the total size for this disk image */
    Footer->current_size = HOST_TO_BE64(VhdFile->TotalSize);
    Footer->orig_size = Footer->current_size; // Original size of the image == the current size

    /* Derive a CHS geometry from the disk image size */
    Footer->cyls  = HOST_TO_BE16(VhdFile->DiskInfo.Cylinders);
    Footer->heads = VhdFile->DiskInfo.Heads;
    Footer->secs_per_cyl = VhdFile->DiskInfo.Sectors;

    if (Type == VHD_FIXED)
        Footer->data_offset = ~0ULL;
    else // if (Type == VHD_DYNAMIC || Type == VHD_DIFFERENCING)
        Footer->data_offset = HOST_TO_BE64(sizeof(VHD_FOOTER));
        // 'Data' (i.e. the BAT, etc...) starts just after the VHD "footer" header.

    Footer->in_saved_state = FALSE;

    RtlZeroMemory(&Footer->Padding, sizeof(Footer->Padding));

    /* Compute the footer checksum */
    Footer->checksum = 0;
    Footer->checksum = HOST_TO_BE32(VhdComputeChecksum(0, Footer, sizeof(*Footer)));




    VhdFile->EndOfData.QuadPart = 0ULL;

    /* For dynamic or differencing disks, also initialize the header */
    if (Type == VHD_DYNAMIC || Type == VHD_DIFFERENCING)
    {
        Status = VhdpCreateDynamicDisk(VhdFile,
                                       ParentIdentifier,
                                       ParentTimeStamp,
                                       ParentPath,
                                       ParentLocatorInfoArray,
                                       NumberOfParentLocators);
    }
    else
    {
        /* Advance the end of the disk */
        VhdFile->EndOfData.QuadPart = TotalSize;
        // ASSERT that this is still VHD_SECTOR_SIZE aligned...

        if (WipeOut)
        {
            /* Explicitely set the image file size (extending the disk image) */
            Status = VhdFile->FileSetSize(VhdFile, TotalSize, 0);
            if (!NT_SUCCESS(Status))
                DPRINT1("FileSetSize() failed (Status 0x%08lx)\n", Status);
        }
    }

    /* Write an updated footer (extending the disk image) */
    FileOffset = VhdFile->EndOfData;
    BytesToWrite = sizeof(VhdFile->Footer); // sizeof(VHD_FOOTER);
    Status = VhdFile->FileWrite(VhdFile,
                                &FileOffset, // &VhdFile->EndOfData,
                                &VhdFile->Footer,
                                BytesToWrite,
                                &BytesToWrite);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileWrite() failed (Status 0x%08lx)\n", Status);

    return Status;
}

NTSTATUS
NTAPI
VhdOpenDisk(
    IN OUT PVHDFILE VhdFile,
    IN UINT64   FileSize,
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

    /*
     * Go to the end of the file and read the footer.
     * As the footer might be 511-byte large only (as found in VHD disks
     * made with versions prior to MS Virtual PC 2004) instead of 512 bytes,
     * check for 511-byte only & round it down to a VHD sector size.
     */
    VhdFile->EndOfData.QuadPart = ROUND_DOWN(FileSize - sizeof(VHD_FOOTER) + 1, (UINT64)VHD_SECTOR_SIZE);

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

    if (VhdVerifyFooter(&Footer))
    {
        /* The footer is valid, read the image type */
        VhdFile->Type = BE32_TO_HOST(Footer.type);

        /*
         * For dynamic & differencing disks, go to the beginning
         * of the image and read its header. It must be identical
         * to the footer.
         */
        if (VhdFile->Type == VHD_DYNAMIC ||
            VhdFile->Type == VHD_DIFFERENCING) // VhdFile->Type != VHD_FIXED
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
                // return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
            }
        }
    }
    else
    {
        /*
         * The footer is corrupt and we cannot rely on it. Try to read any
         * possibly existing "footer" header at the beginning of the file,
         * in case the VHD file is actually one of a dynamic or differencing disk.
         */
        FileOffset.QuadPart = 0LL;
        BytesToRead = sizeof(VHD_FOOTER);
        Status = VhdFile->FileRead(VhdFile,
                                   &FileOffset,
                                   &Footer,
                                   BytesToRead,
                                   &BytesToRead);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("FileRead() failed when reading HDD header (Status 0x%08lx)\n", Status);
            return Status;
        }

        /* Verify the validity of the header */
        if (VhdVerifyFooter(&Footer))
        {
            /* Header ok, use it as our footer */
        }
        else
        {
            /* Header bad, the image is corrupt */
            DPRINT1("OpenHDD: Invalid HDD image (expected VHD).");
            return STATUS_UNRECOGNIZED_MEDIA; // STATUS_FILE_INVALID;
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

    /* Update its read/write state; override it with the SavedState flag */
    VhdFile->ReadOnly = ReadOnly || Footer.in_saved_state;

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
VhdReadDiskAligned(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    ASSERT(VhdFile);

    // STATUS_NONEXISTENT_SECTOR
    // STATUS_INVALID_DEVICE_REQUEST

    if (ReadLength)
        *ReadLength = 0;

    if (// !IS_ALIGNED(Buffer, VHD_SECTOR_SIZE) ||
        !IS_ALIGNED(Length, (SIZE_T)VHD_SECTOR_SIZE) ||
        !IS_ALIGNED(ByteOffset->QuadPart, (LONGLONG)VHD_SECTOR_SIZE))
    {
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    if (ByteOffset->QuadPart + Length > VhdFile->TotalSize)
        return STATUS_INVALID_PARAMETER;

    return Table[VhdFile->Type].ReadDisk(VhdFile,
                                         ByteOffset,
                                         Buffer,
                                         Length,
                                         ReadLength);
}

NTSTATUS
NTAPI
VhdWriteDiskAligned(
    IN  PVHDFILE VhdFile,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    ASSERT(VhdFile);

    if (WrittenLength)
        *WrittenLength = 0;

    if (VhdFile->ReadOnly)
        return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    if (// !IS_ALIGNED(Buffer, VHD_SECTOR_SIZE) ||
        !IS_ALIGNED(Length, (SIZE_T)VHD_SECTOR_SIZE) ||
        !IS_ALIGNED(ByteOffset->QuadPart, (LONGLONG)VHD_SECTOR_SIZE))
    {
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    if (ByteOffset->QuadPart + Length > VhdFile->TotalSize)
        return STATUS_INVALID_PARAMETER;

    return Table[VhdFile->Type].WriteDisk(VhdFile,
                                          ByteOffset,
                                          Buffer,
                                          Length,
                                          WrittenLength);
}

UINT64
NTAPI
VhdGetVirtTotalSize(
    IN PVHDFILE VhdFile)
{
    ASSERT(VhdFile);
    return VhdFile->TotalSize;
}

UINT64
NTAPI
VhdGetFileSize(
    IN PVHDFILE VhdFile)
{
    ASSERT(VhdFile);
    return VhdFile->EndOfData.QuadPart + sizeof(VhdFile->Footer); // sizeof(VHD_FOOTER);
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
