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

#ifndef __FREELDR_H
#define __FREELDR_H

/* Enabled for supporting the deprecated boot options
 * that will be removed in a future FreeLdr version */
#define HAS_DEPRECATED_OPTIONS

#define UINT64_C(val) val##ULL
#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))

#define ROUND_DOWN(n, align) \
    (((ULONG)n) & ~((align) - 1l))

#define ROUND_UP(n, align) \
    ROUND_DOWN(((ULONG)n) + (align) - 1, (align))

/* Public headers */
#ifdef __REACTOS__

//#include <stdio.h>
#include <stdlib.h>
//#include <ctype.h>

#ifndef MY_WIN32

#include <ntddk.h>
#include <ntifs.h>
#include <ioaccess.h>
#include <arc/arc.h>
#include <ndk/asm.h>
#include <ndk/ketypes.h>
#include <ndk/mmtypes.h>
#include <ndk/rtlfuncs.h>
#include <ndk/ldrtypes.h>
#include <ndk/halfuncs.h>
#include <ntdddisk.h>
#include <internal/hal.h>
#include <drivers/pci/pci.h>
#include <winerror.h>
#include <ntstrsafe.h>

#else

#include <ntstatus.h>
#define WIN32_LEAN_AND_MEAN
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#undef CreateDirectory

#if 0
    #include <ntddk.h>
    //#define NTOS_MODE_USER
    //#include <ndk/rtlfuncs.h>
    #include <ndk/rtltypes.h>
    #include <ndk/ldrtypes.h>

    //#include <ntdef.h>
    //#include <winnt.h>
    #include <mmtypes.h>
#else
    #ifndef __REACTOS__
    //#include <winternl.h>
    //#include <devioctl.h>
    //#include <ntdddisk.h>
    #else
    #include <ndk/iofuncs.h>
    #include <ndk/obfuncs.h>
    #include <ndk/rtlfuncs.h>
    #endif
    //#include <ndk/mmtypes.h>
#endif

/* See xdk/mmtypes.h included in WDK */
typedef ULONG_PTR PFN_NUMBER, *PPFN_NUMBER;
#include <arc/arc.h>
#define KSEG0_BASE  __ImageBase // HACK since we compile here in user-mode

// #include <ntdddisk.h> // For PARTITION_STYLE_BRFR
#include <winioctl.h> // for replacing ntdddisk.h
#define PARTITION_STYLE_BRFR 128
typedef struct _DEVICE_OBJECT DEVICE_OBJECT, *PDEVICE_OBJECT;
//typedef PVOID PDEVICE_OBJECT;

PVOID
NTAPI
ExAllocatePool(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes);

PVOID
NTAPI
ExAllocatePoolWithTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag);

VOID
NTAPI
ExFreePool(
    _Pre_notnull_ PVOID P);

VOID
NTAPI
ExFreePoolWithTag(
    _Pre_notnull_ PVOID P,
    _In_ ULONG Tag);

#include <drivers/pci/pci.h>
#include <ntstrsafe.h>

#endif // MY_WIN32

#else
#include <ntsup.h>
#endif

/* Internal headers */
// #include <arcemul.h>
#include <arcname.h>
#include <arcsupp.h>
#include <bytesex.h>
#include <cache.h>
#include <cmdline.h>
#include <comm.h>
#include <disk.h>
#include <fs.h>
#include <inifile.h>
#include <keycodes.h>
#include <linux.h>
#include <custom.h>
#include <miscboot.h>
#include <machine.h>
#include <mm.h>
#include <multiboot.h>
#include <options.h>
#include <oslist.h>
#include <ramdisk.h>
#include <ver.h>

/* NTOS loader */
#include <include/ntldr/winldr.h>
#include <conversion.h> // More-or-less related to MM also...
#include <peloader.h>

/* File system headers */
#include <fs/ext2.h>
#include <fs/fat.h>
#include <fs/ntfs.h>
#include <fs/iso.h>
#include <fs/pxe.h>
#include <fs/btrfs.h>

/* UI support */
#define printf TuiPrintf
#include <ui.h>
#include <ui/video.h>

/* Arch specific includes */
#include <arch/archwsup.h>
#include <arch.h>


VOID __cdecl BootMain(IN PCCH CmdLine);

#ifdef HAS_DEPRECATED_OPTIONS
VOID
WarnDeprecated(
    _In_ PCSTR MsgFmt,
    ...);
#endif

VOID
LoadOperatingSystem(
    _In_ OperatingSystemItem* OperatingSystem);

#ifdef HAS_OPTION_MENU_EDIT_CMDLINE
VOID
EditOperatingSystemEntry(
    _Inout_ OperatingSystemItem* OperatingSystem);
#endif

VOID RunLoader(VOID);
VOID FrLdrCheckCpuCompatibility(VOID);

#endif  /* __FREELDR_H */
