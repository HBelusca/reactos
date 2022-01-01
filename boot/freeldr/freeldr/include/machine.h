/*
 *  FreeLoader
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

#pragma once

#ifndef __DISK_H
#include "disk.h"
#endif

#ifndef __MEMORY_H
#include "mm.h"
#endif

typedef enum _VIDEODISPLAYMODE
{
    VideoTextMode,
    VideoGraphicsMode
} VIDEODISPLAYMODE, *PVIDEODISPLAYMODE;

typedef struct _CONSOLE_VTBL
{
    /* Input */
    BOOLEAN (*ConsKbHit)(VOID); // GetReadStatus()
    int (*ConsGetCh)(VOID);     // Read()

    /* Output */
    VOID (*ConsPutChar)(int Ch);    // Write()
    VOID (*VideoPutChar)(int Ch, UCHAR Attr, unsigned X, unsigned Y); // == Seek() + Write()

    VOID (*VideoSetTextCursorPosition)(UCHAR X, UCHAR Y);   // Seek(), or Write() ANSI escape
    VOID (*VideoHideShowTextCursor)(BOOLEAN Show);

    VOID (*VideoClearScreen)(UCHAR Attr);
    VIDEODISPLAYMODE (*VideoSetDisplayMode)(char *DisplayMode, BOOLEAN Init);   // SetFileInformation()

    // Capability
    VOID (*VideoGetDisplaySize)(PULONG Width, PULONG Height, PULONG Depth);
    ULONG (*VideoGetBufferSize)(VOID);

    // HW stuff
    VOID (*VideoGetFontsFromFirmware)(PULONG RomFontPointers); // FIXME: Doubt

    VOID (*VideoCopyOffScreenBufferToVRAM)(PVOID Buffer);   // Write()

    // Capability
    BOOLEAN (*VideoIsPaletteFixed)(VOID);

    // Ex functionality
    VOID (*VideoSetPaletteColor)(UCHAR Color, UCHAR Red, UCHAR Green, UCHAR Blue);
    VOID (*VideoGetPaletteColor)(UCHAR Color, UCHAR* Red, UCHAR* Green, UCHAR* Blue);

    VOID (*VideoSync)(VOID); // FIXME: Doubt

    // VOID (*Beep)(VOID);
    // VOID (*HwIdle)(VOID);
} CONSOLE_VTBL, *PCONSOLE_VTBL;

typedef struct tagMACHVTBL
{
    CONSOLE_VTBL Console;

    VOID (*Beep)(VOID);
    VOID (*PrepareForReactOS)(VOID);

    // NOTE: Not in the machine.c ...
    FREELDR_MEMORY_DESCRIPTOR* (*GetMemoryDescriptor)(FREELDR_MEMORY_DESCRIPTOR* Current);
    PFREELDR_MEMORY_DESCRIPTOR (*GetMemoryMap)(PULONG MaxMemoryMapSize);
    VOID (*GetExtendedBIOSData)(PULONG ExtendedBIOSDataArea, PULONG ExtendedBIOSDataSize);

    UCHAR (*GetFloppyCount)(VOID);
    BOOLEAN (*DiskReadLogicalSectors)(UCHAR DriveNumber, ULONGLONG SectorNumber, ULONG SectorCount, PVOID Buffer);
    BOOLEAN (*DiskGetDriveGeometry)(UCHAR DriveNumber, PGEOMETRY DriveGeometry);
    ULONG (*DiskGetCacheableBlockCount)(UCHAR DriveNumber);

    // NOTE: In the machine.c under the name of "ArcGetXXXTime"
    TIMEINFO* (*GetTime)(VOID);
    ULONG (*GetRelativeTime)(VOID);

    // NOTE: Not in the machine.c ...
    BOOLEAN (*InitializeBootDevices)(VOID);
    PCONFIGURATION_COMPONENT_DATA (*HwDetect)(VOID);
    VOID (*HwIdle)(VOID);
} MACHVTBL, *PMACHVTBL;

extern MACHVTBL MachVtbl;

/* NOTE: Implemented by each architecture */
VOID MachInit(const char *CmdLine);

/* Console support */
#define MachConsKbHit()     \
    MachVtbl.Console.ConsKbHit()
#define MachConsGetCh()     \
    MachVtbl.Console.ConsGetCh()

#define MachConsPutChar(Ch) \
    MachVtbl.Console.ConsPutChar(Ch)

#define MachVideoPutChar(Ch, Attr, X, Y)    \
    MachVtbl.Console.VideoPutChar((Ch), (Attr), (X), (Y))
#define MachVideoSetTextCursorPosition(X, Y)    \
    MachVtbl.Console.VideoSetTextCursorPosition((X), (Y))
#define MachVideoHideShowTextCursor(Show)   \
    MachVtbl.Console.VideoHideShowTextCursor(Show)

#define MachVideoClearScreen(Attr)  \
    MachVtbl.Console.VideoClearScreen(Attr)
#define MachVideoSetDisplayMode(Mode, Init) \
    MachVtbl.Console.VideoSetDisplayMode((Mode), (Init))
#define MachVideoGetDisplaySize(W, H, D)    \
    MachVtbl.Console.VideoGetDisplaySize((W), (H), (D))
#define MachVideoGetBufferSize()    \
    MachVtbl.Console.VideoGetBufferSize()
#define MachVideoGetFontsFromFirmware(RomFontPointers) \
    MachVtbl.Console.VideoGetFontsFromFirmware(RomFontPointers)
#define MachVideoCopyOffScreenBufferToVRAM(Buf) \
    MachVtbl.Console.VideoCopyOffScreenBufferToVRAM(Buf)
#define MachVideoIsPaletteFixed()   \
    MachVtbl.Console.VideoIsPaletteFixed()
#define MachVideoSetPaletteColor(Col, R, G, B)  \
    MachVtbl.Console.VideoSetPaletteColor((Col), (R), (G), (B))
#define MachVideoGetPaletteColor(Col, R, G, B)  \
    MachVtbl.Console.VideoGetPaletteColor((Col), (R), (G), (B))
#define MachVideoSync() \
    MachVtbl.Console.VideoSync()

/* Machine support */
#define MachBeep()  \
    MachVtbl.Beep()
#define MachPrepareForReactOS() \
    MachVtbl.PrepareForReactOS()
#define MachGetExtendedBIOSData(ExtendedBIOSDataArea, ExtendedBIOSDataSize) \
    MachVtbl.GetExtendedBIOSData((ExtendedBIOSDataArea), (ExtendedBIOSDataSize))
#define MachGetFloppyCount() \
    MachVtbl.GetFloppyCount()
#define MachDiskReadLogicalSectors(Drive, Start, Count, Buf)    \
    MachVtbl.DiskReadLogicalSectors((Drive), (Start), (Count), (Buf))
#define MachDiskGetDriveGeometry(Drive, Geom)   \
    MachVtbl.DiskGetDriveGeometry((Drive), (Geom))
#define MachDiskGetCacheableBlockCount(Drive)   \
    MachVtbl.DiskGetCacheableBlockCount(Drive)

#define MachInitializeBootDevices() \
    MachVtbl.InitializeBootDevices()

#define MachHwDetect()  MachVtbl.HwDetect()
#define MachHwIdle()    MachVtbl.HwIdle()

/* ARC FUNCTIONS **************************************************************/

TIMEINFO* ArcGetTime(VOID);
ULONG ArcGetRelativeTime(VOID);

/* EOF */
