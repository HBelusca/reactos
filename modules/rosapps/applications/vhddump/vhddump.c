// Virtual Disk Dumper v1.1 by HBM

/**

**** The following is an example of possible usage, modeled after an Internet-available shell extension ****
**** I've also gathered some diverse references about VHD manipulation tools thereafter.                ****
**** There are (C) their authors.                                                                       ****

Usage from command-line
VHDShellExt /Action:[Detail|Validate|Mount|UnMount|Expand|Convert|Compact|Merge] /VHD:<Absolute_Path_to_VHD>

/Action:Detail
Retrieves the information about a VHD file. This information includes, size of the VHD, VHD in use, VHD in saved state, VM associated with the VHD, etc.

/Action:Validate
Inspects a given VHD and reports if the file is valid or not.

/Action:Mount
Mounts a VHD file so that the file can be accessed as a disk. This action cannot be performed while the VM using the VHD is running.

/Action:UnMount
Dismounts the previously mounted VHD file.

/Action:Expand
Expands the selected VHD by the given size. This action applies only to fixed and dynamic VHDs. The new size value specified during this action has to be at least 512MB more than the current VHD Max internal size. You should not expand a parent VHD with child VHDs or differencing disks. By doing so, you will invalidate the child VHD. You cannot perform this action while the VM is running.

/Action:Convert
Converts a fixed VHD to Dynamic and vice versa. This operation involves creating a new VHD file and you will be prompted to enter the absolute path to new VHD file. You should not convert a parent VHD with child VHDs or differencing disks. By doing so, you will invalidate the child VHD. You cannot perform this action while the VM is running.

/Action:Compact
Reduces (compactify) the overall size of a dynamic disk or a differencing disk. This action is not applicable to fixed VHDs. You cannot perform this action while the VM is running. You should not compact a parent VHD with child VHDs or differencing disks. By doing so, you will invalidate the child VHD.

/Action:Merge
Merge action works only on differencing disks. This action merges the selected VHD into its immediate parent or a new VHD of your choice. Merge snapshots manually into a VM's VHD might make VM non-functional.

*************************************************

http://www.systola.com/blog/14.01.2015/VhdTool-Is-Dead-Long-Live-VhdxTool

http://code.msdn.microsoft.com/vhdtool
https://web.archive.org/web/20120417024714/http://archive.msdn.microsoft.com/vhdtool

P:foo>VhdTool.exe /create "p:foofoo.vhd" 10737418240
       Status: Creating new fixed format VHD with name "p:foofoo.vhd"
       Status: Attempting to create file "p:foofoo.vhd"
       Status: Created file "p:foofoo.vhd"
       Status: Set the file length
       Status: Set the valid data length
       Status: VHD footer generated.
       Status: VHD footer appended.
       Status: VHD header area cleared.
       Status: Complete

P:foo>dir

Volume in drive P is WD120GB
Volume Serial Number is EC15-9FC3

Directory of P:foo

04/09/2009  06:52 PM    <DIR>          .
04/09/2009  06:52 PM    <DIR>          ..
04/09/2009  06:52 PM    10,737,418,752 foo.vhd

So, the foo.vhd is 10GB on the physical drive.

When I boot into the BING from floppy, BING shows HD0 with an empty area of 130552 MB of free space.

If I go to the VPC guest’s BIOS at boot, it says:

Device    : Hard Disk
Vendor    : Virtual HD
Size      : 136.9GB
LBA Mode  : Supported
Block Mode: 128Sectors
PIO Mode  : 4
Async DMA : MultiWord DMA-2

***********

P:foo>VhdTool.exe /create "p:foofoo.vhd" 13631488
       Status: Creating new fixed format VHD with name "p:foofoo.vhd"
       Status: Attempting to create file "p:foofoo.vhd"
       Status: Created file "p:foofoo.vhd"
       Status: Set the file length
       Status: Set the valid data length
       Status: VHD footer generated.
       Status: VHD footer appended.
       Status: VHD header area cleared.
       Status: Complete

and then I used VPC2007’s VHD wizard to create the same size VHD, foo1.vhd.
Looking a foo.vhd vs. foo1.vhd, it looks like the "Disk Geometry" in the footer is different:

foo.vhd (created by vhdtool.exe):
0x34D3103F

foo1.vhd (created by VPC2007 wizard):
0x01870411

Why is the "Disk Geometry" in the footer in the VHD created by vhdtool.exe different?


https://i2.wp.com/blog.workinghardinit.work/wp-content/uploads/2015/07/image54.png

http://jim.studt.net/depository/?timePrefix=2008-02

https://github.com/andreiw/vhdtool/blob/master/vhdtool.c

http://qemu-devel.nongnu.narkive.com/ceKHTxVJ/patch-v10-support-vhd-type-vhd-differencing

**/

#include "vhddump.h"
#include <conio.h>

#include "virtdisk_driver.h"

NTSTATUS
CopyDiskContents(
    IN PVIRTUAL_DISK VirtualDisk,
    IN PCWSTR CopyFileName,
    IN OUT PVOID TempBuffer,
    IN SIZE_T TempBufferSize)
{
    NTSTATUS Status;
    HANDLE hFile;
    LARGE_INTEGER FileOffset;
    SIZE_T Length, WrittenLength;
    UINT64 RemainingSize;

    /* Try to open the file */
    // SetLastError(0); // For debugging purposes
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    hFile = CreateFileW(CopyFileName,
                        GENERIC_WRITE,
                        FILE_SHARE_READ|FILE_SHARE_WRITE /* No sharing access */,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    DisplayMessage(L"File '%s' opening %s ; GetLastError() = %u\n",
            CopyFileName, hFile != INVALID_HANDLE_VALUE ? L"succeeded" : L"failed", GetLastError());

    /* If we failed, bail out */
    if (hFile == INVALID_HANDLE_VALUE)
    {
        DisplayMessage(L"CopyDiskContents: Error when opening disk file '%s' (Error: %u).", CopyFileName, GetLastError());
        return RtlGetLastNtStatus();
    }

    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    RemainingSize = VdGetVirtTotalSize(VirtualDisk);
    FileOffset.QuadPart = 0LL;
    while (RemainingSize > 0)
    {
        RtlZeroMemory(TempBuffer, TempBufferSize);
        Length = min(RemainingSize, TempBufferSize);
        Status = VdReadDisk(VirtualDisk, &FileOffset, TempBuffer, Length, &Length);
        if (!NT_SUCCESS(Status))
            DisplayMessage(L"Failed to read sectors of the disk, Status 0x%08lx\n", Status);

        WriteFile(hFile, TempBuffer, Length, &WrittenLength, NULL);

        FileOffset.QuadPart += Length;
        RemainingSize -= Length;
    }

    CloseHandle(hFile);
    return STATUS_SUCCESS;
}

void Usage(WCHAR* name)
{
    wprintf(L"Usage: %s vhdfile.vhd\n", name);
}

int wmain(int argc, WCHAR* argv[])
{
    NTSTATUS Status;
    UNICODE_STRING DiskFileName;
    PVIRTUAL_DISK VirtualDisk;
    LARGE_INTEGER FileOffset;
    SIZE_T Length;
    CHAR SectorData[2 * 512]; // Buffer large enough to store two sectors of 512 bytes.
    // PCHAR SectorData;
    ULONG SectorDataSize;


/* H:\rosbuilds\x86_VS10_clean\modules\rosapps\applications\cmdutils\vhddump\test_MSVHD_700Gb - Copie.vhd (09/04/2017 23:25:46)
   StartOffset: 0015E800, EndOffset: 0015E9FF, Length: 00000200 */

unsigned char rawData[512] = {
	0x33, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8E, 0xC0, 0x8E, 0xD8, 0xBE,
	0x00, 0x7C, 0xBF, 0x00, 0x06, 0xB9, 0x00, 0x02, 0xFC, 0xF3, 0xA4, 0x50,
	0x68, 0x1C, 0x06, 0xCB, 0xFB, 0xB9, 0x04, 0x00, 0xBD, 0xBE, 0x07, 0x80,
	0x7E, 0x00, 0x00, 0x7C, 0x0B, 0x0F, 0x85, 0x0E, 0x01, 0x83, 0xC5, 0x10,
	0xE2, 0xF1, 0xCD, 0x18, 0x88, 0x56, 0x00, 0x55, 0xC6, 0x46, 0x11, 0x05,
	0xC6, 0x46, 0x10, 0x00, 0xB4, 0x41, 0xBB, 0xAA, 0x55, 0xCD, 0x13, 0x5D,
	0x72, 0x0F, 0x81, 0xFB, 0x55, 0xAA, 0x75, 0x09, 0xF7, 0xC1, 0x01, 0x00,
	0x74, 0x03, 0xFE, 0x46, 0x10, 0x66, 0x60, 0x80, 0x7E, 0x10, 0x00, 0x74,
	0x26, 0x66, 0x68, 0x00, 0x00, 0x00, 0x00, 0x66, 0xFF, 0x76, 0x08, 0x68,
	0x00, 0x00, 0x68, 0x00, 0x7C, 0x68, 0x01, 0x00, 0x68, 0x10, 0x00, 0xB4,
	0x42, 0x8A, 0x56, 0x00, 0x8B, 0xF4, 0xCD, 0x13, 0x9F, 0x83, 0xC4, 0x10,
	0x9E, 0xEB, 0x14, 0xB8, 0x01, 0x02, 0xBB, 0x00, 0x7C, 0x8A, 0x56, 0x00,
	0x8A, 0x76, 0x01, 0x8A, 0x4E, 0x02, 0x8A, 0x6E, 0x03, 0xCD, 0x13, 0x66,
	0x61, 0x73, 0x1C, 0xFE, 0x4E, 0x11, 0x75, 0x0C, 0x80, 0x7E, 0x00, 0x80,
	0x0F, 0x84, 0x8A, 0x00, 0xB2, 0x80, 0xEB, 0x84, 0x55, 0x32, 0xE4, 0x8A,
	0x56, 0x00, 0xCD, 0x13, 0x5D, 0xEB, 0x9E, 0x81, 0x3E, 0xFE, 0x7D, 0x55,
	0xAA, 0x75, 0x6E, 0xFF, 0x76, 0x00, 0xE8, 0x8D, 0x00, 0x75, 0x17, 0xFA,
	0xB0, 0xD1, 0xE6, 0x64, 0xE8, 0x83, 0x00, 0xB0, 0xDF, 0xE6, 0x60, 0xE8,
	0x7C, 0x00, 0xB0, 0xFF, 0xE6, 0x64, 0xE8, 0x75, 0x00, 0xFB, 0xB8, 0x00,
	0xBB, 0xCD, 0x1A, 0x66, 0x23, 0xC0, 0x75, 0x3B, 0x66, 0x81, 0xFB, 0x54,
	0x43, 0x50, 0x41, 0x75, 0x32, 0x81, 0xF9, 0x02, 0x01, 0x72, 0x2C, 0x66,
	0x68, 0x07, 0xBB, 0x00, 0x00, 0x66, 0x68, 0x00, 0x02, 0x00, 0x00, 0x66,
	0x68, 0x08, 0x00, 0x00, 0x00, 0x66, 0x53, 0x66, 0x53, 0x66, 0x55, 0x66,
	0x68, 0x00, 0x00, 0x00, 0x00, 0x66, 0x68, 0x00, 0x7C, 0x00, 0x00, 0x66,
	0x61, 0x68, 0x00, 0x00, 0x07, 0xCD, 0x1A, 0x5A, 0x32, 0xF6, 0xEA, 0x00,
	0x7C, 0x00, 0x00, 0xCD, 0x18, 0xA0, 0xB7, 0x07, 0xEB, 0x08, 0xA0, 0xB6,
	0x07, 0xEB, 0x03, 0xA0, 0xB5, 0x07, 0x32, 0xE4, 0x05, 0x00, 0x07, 0x8B,
	0xF0, 0xAC, 0x3C, 0x00, 0x74, 0x09, 0xBB, 0x07, 0x00, 0xB4, 0x0E, 0xCD,
	0x10, 0xEB, 0xF2, 0xF4, 0xEB, 0xFD, 0x2B, 0xC9, 0xE4, 0x64, 0xEB, 0x00,
	0x24, 0x02, 0xE0, 0xF8, 0x24, 0x02, 0xC3, 0x49, 0x6E, 0x76, 0x61, 0x6C,
	0x69, 0x64, 0x20, 0x70, 0x61, 0x72, 0x74, 0x69, 0x74, 0x69, 0x6F, 0x6E,
	0x20, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x00, 0x45, 0x72, 0x72, 0x6F, 0x72,
	0x20, 0x6C, 0x6F, 0x61, 0x64, 0x69, 0x6E, 0x67, 0x20, 0x6F, 0x70, 0x65,
	0x72, 0x61, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x73, 0x79, 0x73, 0x74, 0x65,
	0x6D, 0x00, 0x4D, 0x69, 0x73, 0x73, 0x69, 0x6E, 0x67, 0x20, 0x6F, 0x70,
	0x65, 0x72, 0x61, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x73, 0x79, 0x73, 0x74,
	0x65, 0x6D, 0x00, 0x00, 0x00, 0x63, 0x7B, 0x9A, 0x9E, 0xC6, 0x44, 0x92,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xAA
};



    if (argc <= 1)
    {
        Usage(argv[0]);
        return 0;
    }

#if 1
    argv[1] = L"test_new_disk - Copie.vhd";
    DisplayMessage(L"Opening disk %s\n", argv[1]);
    RtlInitUnicodeString(&DiskFileName, argv[1]);
    Status = VdOpenDisk(&VirtualDisk, &DiskFileName, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DisplayMessage(L"OpenDisk failed to open disk %s, Status 0x%08lx\n", argv[1], Status);
        return -1;
    }
#else
    argv[1] = L"test_new_disk.vhd";
    DisplayMessage(L"Creating disk %s\n", argv[1]);
    RtlInitUnicodeString(&DiskFileName, argv[1]);
    Status = VdCreateDisk(&VirtualDisk, &DiskFileName, 700ULL*1024 /*3*/ * 1024 * 1024);
    if (!NT_SUCCESS(Status))
    {
        DisplayMessage(L"CreateDisk failed to create disk %s, Status 0x%08lx\n", argv[1], Status);
        return -1;
    }
#endif

    wprintf(L"Press any key to continue...\n");
    _getch();

#if 1
    /* Read the first two sectors at offset 0 */
    RtlZeroMemory(SectorData, sizeof(SectorData));
    FileOffset.QuadPart = 0LL;
    Length = sizeof(SectorData);
    Status = VdReadDisk(VirtualDisk, &FileOffset, SectorData, Length, &Length);
    if (!NT_SUCCESS(Status))
        DisplayMessage(L"Failed to read the first 2 sectors of the disk, Status 0x%08lx\n", Status);

    /* Read two sectors starting sector 127 (sector 128 contains a FAT) */
    RtlZeroMemory(SectorData, sizeof(SectorData));
    FileOffset.QuadPart = 651263LL * 512;
    Length = sizeof(SectorData);
    Status = VdReadDisk(VirtualDisk, &FileOffset, SectorData, Length, &Length);
    if (!NT_SUCCESS(Status))
        DisplayMessage(L"Failed to read 2 sectors of the disk, Status 0x%08lx\n", Status);
#else
    /* Read the first two sectors at offset 0 */
    // SectorDataSize = /**/11;/* 2098171;*/ // 4098 * 512; // 3000 sectors
    SectorDataSize = 4*PAGE_SIZE;
    SectorData = RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, SectorDataSize);
    //RtlZeroMemory(SectorData, SectorDataSize);
    FileOffset.QuadPart = 7LL;
    Length = SectorDataSize;
    Status = VdReadDisk(VirtualDisk, &FileOffset, SectorData, Length, &Length);
    if (!NT_SUCCESS(Status))
        DisplayMessage(L"Failed to read the first 2 sectors of the disk, Status 0x%08lx\n", Status);
#endif

    wprintf(L"Press any key to continue...\n");
    _getch();

#if 1
    /* Write one byte at the last byte of the disk */
    // SectorData[0] = 0xCA;
    // FileOffset.QuadPart = VdGetVirtTotalSize(VirtualDisk) - 1;
    // Length = 1;
    // FileOffset.QuadPart = 2;
    FileOffset.QuadPart = 651263LL * 512 + 256;
    Length = sizeof(rawData);
    Status = VdWriteDisk(VirtualDisk, &FileOffset, /*SectorData*/rawData, Length, &Length);
    if (!NT_SUCCESS(Status))
        DisplayMessage(L"Failed to read the first 2 sectors of the disk, Status 0x%08lx\n", Status);

    wprintf(L"Press any key to continue...\n");
    _getch();
#endif

    /**/SectorDataSize = sizeof(SectorData);

#if 0
    Status = CopyDiskContents(VirtualDisk, L"my_VHD_copy_dyn.vhd", SectorData, SectorDataSize);
    if (!NT_SUCCESS(Status))
        DisplayMessage(L"Failed to CopyDiskContents of the disk, Status 0x%08lx\n", Status);

    wprintf(L"Press any key to continue...\n");
    _getch();
#endif

    // RtlFreeHeap(GetProcessHeap(), 0, SectorData);

    DisplayMessage(L"Now closing disk %s (0x%p)\n", argv[1], VirtualDisk);
    VdCloseDisk(VirtualDisk);
    VirtualDisk = NULL;

    wprintf(L"Press any key to quit...\n");
    _getch();

    return 0;
}
