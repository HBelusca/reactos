/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Machine Setup
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* GLOBALS ********************************************************************/

extern HANDLE hConIn;

/* FUNCTIONS ******************************************************************/

VOID
Win32HwIdle(VOID)
{
    // Sleep(1);
    YieldProcessor();
}

VOID
Win32PrepareForReactOS(VOID)
{
    // PcVideoPrepareForReactOS();
    // DiskStopFloppyMotor();

    // Win32ConVideoClearScreenColor(MAKE_COLOR(0, 0, 0), TRUE);
    // Win32ConVideoHideShowTextCursor(FALSE);
}

VOID
MachInit(const char *CmdLine)
{
    RtlZeroMemory(&MachVtbl, sizeof(MachVtbl));

    MachVtbl.ConsPutChar = Win32ConsPutChar;
    MachVtbl.ConsKbHit = Win32ConsKbHit;
    MachVtbl.ConsGetCh = Win32ConsGetCh;
    MachVtbl.VideoClearScreen = Win32VideoClearScreen;
    MachVtbl.VideoSetDisplayMode = Win32VideoSetDisplayMode;
    MachVtbl.VideoGetDisplaySize = Win32VideoGetDisplaySize;
    MachVtbl.VideoGetBufferSize = Win32VideoGetBufferSize;
    MachVtbl.VideoGetFontsFromFirmware = Win32VideoGetFontsFromFirmware;
    MachVtbl.VideoSetTextCursorPosition = Win32VideoSetTextCursorPosition;
    MachVtbl.VideoHideShowTextCursor = Win32VideoHideShowTextCursor;
    MachVtbl.VideoPutChar = Win32VideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = Win32VideoCopyOffScreenBufferToVRAM;
    MachVtbl.VideoIsPaletteFixed = Win32VideoIsPaletteFixed;
    MachVtbl.VideoSetPaletteColor = Win32VideoSetPaletteColor;
    MachVtbl.VideoGetPaletteColor = Win32VideoGetPaletteColor;
    MachVtbl.VideoSync = Win32VideoSync;
    MachVtbl.Beep = Win32Beep;
    MachVtbl.PrepareForReactOS = Win32PrepareForReactOS;
    MachVtbl.GetMemoryMap = Win32MemGetMemoryMap;
    MachVtbl.GetExtendedBIOSData = Win32GetExtendedBIOSData;
    MachVtbl.GetFloppyCount = Win32GetFloppyCount;
    MachVtbl.DiskReadLogicalSectors = Win32DiskReadLogicalSectors;
    MachVtbl.DiskGetDriveGeometry = Win32DiskGetDriveGeometry;
    MachVtbl.DiskGetCacheableBlockCount = Win32DiskGetCacheableBlockCount;
    MachVtbl.GetTime = Win32GetTime;
    MachVtbl.InitializeBootDevices = Win32InitializeBootDevices;
    MachVtbl.HwDetect = Win32HwDetect;
    MachVtbl.HwIdle = Win32HwIdle;

    /* Setup graphics */
    if (!Win32VideoInit())
    {
        ERR("Failed to setup graphics\n");
    }
    /* For the console */
    hConIn = GetStdHandle(STD_INPUT_HANDLE);
}

/* EOF */
