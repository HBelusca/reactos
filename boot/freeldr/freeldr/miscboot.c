/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

/* FUNCTIONS ******************************************************************/

#if defined(_M_IX86) || defined(_M_AMD64)

// typedef USHORT uint16_t;
typedef ULONG addr_t;

/* First usable and first unusable offsets */
#define dosmin ((addr_t)0x0500)
// #define dosmax ((addr_t)(*(uint16_t*) 0x413 << 10)) // BDA 40:13 (word) Memory size in KBytes (see INT 12)
#define dosmax ((addr_t)0xA0000)

static BOOLEAN
ParseSegOffIp(
    _In_ PCSTR ptr,
    _Out_ addr_t* seg,
    _Out_ addr_t* off,
    _Out_ addr_t* ip)
{
    addr_t segval, offval, ipval, val;
    PCCH p;

    segval = 0; // *seg;
    offval = 0; // *off;
    ipval  = 0; // *ip;

    // p = ptr; if (p[0] && p[0] != ':')
    segval = strtoul(ptr, (PCHAR*)&p, 0);
    if (p[0] == ':' && p[1] /*&& p[1] != ':'*/)
        offval = strtoul(p+1, (PCHAR*)&p, 0);
    if (p[0] == ':' && p[1] /*&& p[1] != ':'*/)
        ipval = strtoul(p+1, NULL, 0);

    /* Verify if load address is within [dosmin, dosmax) */
    val = (segval << 4) + offval;

    if (val < dosmin || val >= dosmax)
    {
        ERR("Invalid seg:off:* address specified.");
        return FALSE;
    }

    /* Verify if jump address is within [dosmin, dosmax)
     * and offset is 16-bit sane */
    val = (segval << 4) + ipval;

    if (ipval > 0xFFFE || val < dosmin || val >= dosmax)
    {
        ERR("Invalid seg:*:ip address specified.");
        return FALSE;
    }

    *seg = segval;
    *off = offval;
    *ip  = ipval;

    return TRUE;
}


#if defined(SARCH_PC98)

static // inline
ULONG
Pc98GetBootSectorLoadAddress(
    _In_ PPC98_DISK_DRIVE DiskDrive)
{
    if (((DiskDrive->DaUa & 0xF0) == 0x30) ||
        ((DiskDrive->DaUa & 0xF0) == 0xB0))
    {
        /* 1.44 MB floppy */
        return 0x1FE00;
    }
    else if (DiskDrive->Type & DRIVE_FDD)
    {
        return 0x1FC00;
    }
    return 0x1F800;
}

static VOID __cdecl
ChainLoadBiosBootSectorCode(
    _In_ addr_t Seg,
    _In_ addr_t Off,
    _In_ addr_t Ip,
    _In_opt_ UCHAR BootDrive,
    _In_opt_ ULONG BootPartition)
{
    REGS Regs = {0};
    PPC98_DISK_DRIVE DiskDrive;

    DiskDrive = Pc98DiskDriveNumberToDrive(BootDrive);
    if (!DiskDrive)
    {
        ERR("Failed to get drive 0x%x\n", BootDrive);
        return;
    }

    // Seg = (USHORT)(Pc98GetBootSectorLoadAddress(DiskDrive) >> 4);

    Regs.w.ax = DiskDrive->DaUa;
    Regs.w.si = Seg;
    Regs.w.es = Seg;
#ifndef MY_WIN32
    *(PUCHAR)MEM_DISK_BOOT = DiskDrive->DaUa;
#endif /* MY_WIN32 */

    MachVideoClearScreen(ATTR(COLOR_WHITE, COLOR_BLACK)); // MachDefaultTextAttr;

#ifndef MY_WIN32
    Relocator16Boot(&Regs,
                    /* Stack segment:pointer */
                    0x0020, 0x00FF,
                    /* Code segment:pointer */
                    Seg, Ip);
#endif /* MY_WIN32 */
}

#else

static VOID __cdecl
ChainLoadBiosBootSectorCode(
    _In_ addr_t Seg,
    _In_ addr_t Off,
    _In_ addr_t Ip,
    _In_opt_ UCHAR BootDrive,
    _In_opt_ ULONG BootPartition)
{
    REGS Regs = {0};
    USHORT ss, sp;

    /* Set DS and ES to the original segment */
    Regs.w.ds = Regs.w.es = Seg;
    /* Set the stack to the beginning of the boot sector and growing downward */
    ss = Seg;
#if 0
    /* Set the stack to 0000:7C00 only if we are booting something like an MBR or VBR */
    if (Seg == 0 && Ip == 0x7C00)
        sp = 0x7C00;
#else
    // // DOSBOX-X:
    // /* The standard BIOS is said to put its stack (at least at OS boot time)
    //  * 512 bytes past the end of the boot sector, meaning that the boot sector
    //  * loads to 0000:7C00 and the stack is set grow downward from 0000:8000 */
    // sp = 0x8000 - 4;
    sp = Off;
#endif

    /*
     * Set the boot drive and the boot partition.
     * Syslinux NOTE: Some DOS kernels want the drive number in BL instead of DL.
     */
    Regs.b.bl = Regs.b.dl = BootDrive;
    /*
     * NOTE: some MBRs will also set SI (from BP) to the memory offset of
     * the MBR partition table entry for the current partition we boot from.
     * See https://wiki.osdev.org/MBR_(x86)
     * and https://en.wikipedia.org/wiki/Volume_boot_record#Invocation
     *
     * In an MBR, the first entry (index 0) is at 0x01BE, with increments
     * of sizeof(PARTITION_TABLE_ENTRY) == 0x10 bytes:
     *     0x01BE + (BootPartition - 1) * sizeof(PARTITION_TABLE_ENTRY)
     * when a valid partition is specified (BootPartition >= 1), otherwise zero.
     *
     * FIXME: How to create such an entry if we don't have an MBR available already?
     */
    Regs.w.si = Regs.w.bp = 0;

    /*
     * Don't stop the floppy drive motor when we are just booting a bootsector,
     * a drive, or a partition. If we were to stop the floppy motor, the BIOS
     * wouldn't be informed and if the next read is to a floppy then the BIOS
     * will still think the motor is on and this will result in a read error.
     */
    // DiskStopFloppyMotor();

#ifndef MY_WIN32
#ifndef UEFIBOOT
    Relocator16Boot(&Regs,
                    /* Stack segment:pointer */
                    ss, sp,
                    /* Code segment:pointer */
                    Seg, Ip);
#endif
#endif /* MY_WIN32 */
}

#endif /* SARCH_PC98 */


/**
 * @brief
 * Loads and boots a disk MBR, a partition VBR or a file boot sector.
 **/
ARC_STATUS
LoadAndBootSector(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ PCHAR Envp[])
{
    ARC_STATUS Status;
    PCSTR ArgValue;
    PCSTR BootPath;
    PCSTR FileName;
    UCHAR BiosDriveNumber = 0;
    ULONG PartitionNumber = 0;
    addr_t Seg, Off, Ip;
    ULONG LoadAddress;
    ULONG FileId;
    ULONG BytesRead;

#if DBG
    /* Ensure the boot type is the one expected */
    ArgValue = GetArgumentValue(Argc, Argv, "BootType");
    if (!ArgValue || !*ArgValue || _stricmp(ArgValue, "BootSector") != 0)
    {
        ERR("Unexpected boot type '%s', aborting\n", ArgValue ? ArgValue : "n/a");
        return EINVAL;
    }
#endif

    /* Find all the message box settings and run them */
    UiShowMessageBoxesInArgv(Argc, Argv);

    /* Check whether we have a "BootPath" value. If we don't have one,
     * fall back to using the system partition as default path. */
    BootPath = GetArgumentValue(Argc, Argv, "BootPath");
    if (!BootPath || !*BootPath)
        BootPath = GetArgumentValue(Argc, Argv, "SystemPartition");

    /*
     * Retrieve the BIOS drive and partition numbers; verify also that the
     * path is "valid" in the sense that it must not contain any file name.
     */
    FileName = NULL;
    if (!DissectArcPath(BootPath, &FileName, &BiosDriveNumber, &PartitionNumber) ||
        (FileName && *FileName))
    {
        UiMessageBox("Currently unsupported BootPath value:\n%s", BootPath);
        return EINVAL;
    }

    FileName = NULL;
    if (strstr(BootPath, ")partition()") || strstr(BootPath, ")partition(0)"))
    {
        /*
         * The partition specifier is zero i.e. the device is accessed
         * in an unpartitioned fashion, do not retrieve a file name.
         *
         * NOTE: If we access a floppy drive, we would not have a
         * partition specifier, and PartitionNumber would be == 0,
         * so don't check explicitly for PartitionNumber because
         * we want to retrieve a file name.
         */
    }
    else
    {
        /* Retrieve the file name, if any, and normalize
         * the pointer to make subsequent tests simpler */
        FileName = GetArgumentValue(Argc, Argv, "BootSectorFile");
        if (FileName && !*FileName)
            FileName = NULL;
    }


    /* If we load a boot sector file, reset the drive number
     * so as to use the original boot drive/partition */
    if (FileName)
        BiosDriveNumber = 0;
    if (!BiosDriveNumber)
    {
        BiosDriveNumber = FrldrBootDrive;
        PartitionNumber = FrldrBootPartition;
    }


    /* Initialize standard values for load and jump segment/offsets */
#if defined(SARCH_PC98)
    LoadAddress = Pc98GetBootSectorLoadAddress(BiosDriveNumber);
    Seg = (LoadAddress >> 4), Off = Ip = (LoadAddress & 0x0F);
#else
    Seg = 0, Off = Ip = 0x7C00;
    LoadAddress = (Seg << 4) + Off;
#endif

    /* Retrieve load and jump segment/offsets */
    ArgValue = GetArgumentValue(Argc, Argv, "Sect");
    if (ArgValue)
    {
        if (!ParseSegOffIp(ArgValue, &Seg, &Off, &Ip))
            return EINVAL;
        /* Recalculate the linear loading address */
        LoadAddress = (Seg << 4) + Off;
    }


    /* Open the boot sector file or the volume */
    if (FileName)
        Status = FsOpenFile(FileName, BootPath, OpenReadOnly, &FileId);
    else
        Status = ArcOpen((PSTR)BootPath, OpenReadOnly, &FileId);
    if (Status != ESUCCESS)
    {
        UiMessageBox("Unable to open %s", FileName ? FileName : BootPath);
        return Status;
    }

    // TODO: Allow the possibility to specify the offset and size to read?
    /*
     * Now try to load the boot sector: disk MBR (when PartitionNumber == 0),
     * partition VBR or boot sector file. If this fails, abort.
     */
    Status = ArcRead(FileId, UlongToPtr(LoadAddress), 512, &BytesRead);
    ArcClose(FileId);
    if ((Status != ESUCCESS) || (BytesRead != 512))
    {
        PCSTR WhatFailed;

        if (FileName)
            WhatFailed = "boot sector file";
        else if (PartitionNumber != 0)
            WhatFailed = "partition's boot sector";
        else
            WhatFailed = "MBR boot sector";

        UiMessageBox("Unable to load %s.", WhatFailed);
        return EIO;
    }

    TRACE("\n**** BOOT SECTOR LOADED AT 0x%p (0x%04x:0x%04x), %lu bytes ****\n",
          UlongToPtr(LoadAddress), Seg, Off, 512);
    DbgDumpBuffer(DPRINT_DISK, UlongToPtr(LoadAddress), 512);

    /* Check for validity */
    // TODO: Make this check optional?
    if (*(USHORT*)UlongToPtr(LoadAddress + 0x1FE) != 0xAA55)
    {
        UiMessageBox("Invalid boot sector magic (0xAA55)");
        return ENOEXEC;
    }

    UiUnInitialize("Booting...");
    IniCleanup();

#ifndef UEFIBOOT
    /* Boot the loaded sector code */
    ChainLoadBiosBootSectorCode(Seg, Off, Ip, BiosDriveNumber, PartitionNumber);
#endif
    /* Must not return! */
    return ESUCCESS;
}

#endif /* _M_IX86 || _M_AMD64 */

#if 0
/**
 * @brief
 * Loads and boots a storage device in a "standard" way for the
 * current platform.
 *
 * BIOS-based PCs:
 *
 * - Hard disks are booted from their MBR code, and partitions are
 *   booted from their VBR code. Standard loading addresses and
 *   sector sizes (512 bytes) are used.
 *
 * - Floppy disks are booted from their VBR code. Standard loading
 *   addresses and sector sizes (512 bytes) are used.
 *
 * - CD-ROM disks are booted following the El-Torito specification.
 *
 * ARC-based PCs:
 *
 * - Either existing boot entry, or manual file choosing.
 *
 * (u)EFI-based PCs:
 *
 * - CD-ROM disks follow El-Torito. (u)EFI looks for the FAT-formatted
 *   "partition" in it and tries to run /EFI/BOOT/BOOT<platform>.EFI
 **/
ARC_STATUS
LoadAndBootDevice(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ PCHAR Envp[])
{
    return ENODEV;
}
#endif

/* EOF */
