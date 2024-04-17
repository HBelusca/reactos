/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware-specific routines for NEC PC-98 series
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* GLOBALS ********************************************************************/

BOOLEAN HiResoMachine;

/* FUNCTIONS ******************************************************************/

VOID
Pc98GetExtendedBIOSData(PULONG ExtendedBIOSDataArea, PULONG ExtendedBIOSDataSize)
{
    *ExtendedBIOSDataArea = HiResoMachine ? MEM_EXTENDED_HIGH_RESO : MEM_EXTENDED_NORMAL;
    *ExtendedBIOSDataSize = 64;
}

VOID
Pc98HwIdle(VOID)
{
    /* Unimplemented */
}

VOID
Pc98PrepareForReactOS(VOID)
{
    Pc98DiskPrepareForReactOS();
    Pc98VideoPrepareForReactOS();
    DiskStopFloppyMotor();
    DebugDisableScreenPort();
}

static BOOLEAN
Pc98ArchTest(VOID)
{
    REGS RegsIn, RegsOut;

    /* Int 1Ah AX=1000h
     * NEC PC-9800 series - Installation check
     */
    RegsIn.w.ax = 0x1000;
    Int386(0x1A, &RegsIn, &RegsOut);

    return RegsOut.w.ax != 0x1000;
}

VOID
MachInit(const char *CmdLine)
{
    if (!Pc98ArchTest())
    {
        ERR("This is not a supported PC98!\n");

        /* Disable and halt the CPU */
        _disable();
        __halt();

        while (TRUE)
            NOTHING;
    }

    /* Setup vtbl */
    RtlZeroMemory(&MachVtbl, sizeof(MachVtbl));
    MachVtbl.ConsPutChar = Pc98ConsPutChar;
    MachVtbl.ConsKbHit = Pc98ConsKbHit;
    MachVtbl.ConsGetCh = Pc98ConsGetCh;
    MachVtbl.VideoClearScreen = Pc98VideoClearScreen;
    MachVtbl.VideoSetDisplayMode = Pc98VideoSetDisplayMode;
    MachVtbl.VideoGetDisplaySize = Pc98VideoGetDisplaySize;
    MachVtbl.VideoGetBufferSize = Pc98VideoGetBufferSize;
    MachVtbl.VideoGetFontsFromFirmware = Pc98VideoGetFontsFromFirmware;
    MachVtbl.VideoSetTextCursorPosition = Pc98VideoSetTextCursorPosition;
    MachVtbl.VideoHideShowTextCursor = Pc98VideoHideShowTextCursor;
    MachVtbl.VideoPutChar = Pc98VideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = Pc98VideoCopyOffScreenBufferToVRAM;
    MachVtbl.VideoIsPaletteFixed = Pc98VideoIsPaletteFixed;
    MachVtbl.VideoSetPaletteColor = Pc98VideoSetPaletteColor;
    MachVtbl.VideoGetPaletteColor = Pc98VideoGetPaletteColor;
    MachVtbl.VideoSync = Pc98VideoSync;
    MachVtbl.Beep = Pc98Beep;
    MachVtbl.PrepareForReactOS = Pc98PrepareForReactOS;
    MachVtbl.GetMemoryMap = Pc98MemGetMemoryMap;
    MachVtbl.GetExtendedBIOSData = Pc98GetExtendedBIOSData;
    MachVtbl.GetFloppyCount = Pc98GetFloppyCount;
    MachVtbl.DiskReadLogicalSectors = Pc98DiskReadLogicalSectors;
    MachVtbl.DiskGetDriveGeometry = Pc98DiskGetDriveGeometry;
    MachVtbl.DiskGetCacheableBlockCount = Pc98DiskGetCacheableBlockCount;
    MachVtbl.GetTime = Pc98GetTime;
    MachVtbl.InitializeBootDevices = Pc98InitializeBootDevices;
    MachVtbl.HwDetect = Pc98HwDetect;
    MachVtbl.HwIdle = Pc98HwIdle;

    HiResoMachine = *(PUCHAR)MEM_BIOS_FLAG1 & HIGH_RESOLUTION_FLAG;

    HalpCalibrateStallExecution();
    Pc98VideoInit();
}
