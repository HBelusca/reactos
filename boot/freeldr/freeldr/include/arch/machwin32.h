/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Win32 "mach" header
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <machine.h>

VOID
Win32ConsPutChar(int c);

BOOLEAN
Win32ConsKbHit(VOID);

int
Win32ConsGetCh(VOID);

BOOLEAN
Win32VideoInit(VOID);
BOOLEAN
Win32ConVideoInit(VOID);

VOID
Win32VideoClearScreen(
    _In_ UCHAR Attr);
VOID
Win32ConVideoClearScreen(
    _In_ UCHAR Attr);

VIDEODISPLAYMODE
Win32VideoSetDisplayMode(
    _In_ PCSTR DisplayMode,
    _In_ BOOLEAN Init);
VIDEODISPLAYMODE
Win32ConVideoSetDisplayMode(
    _In_ PCSTR DisplayMode,
    _In_ BOOLEAN Init);

VOID
Win32VideoGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth);
VOID
Win32ConVideoGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth);

ULONG
Win32VideoGetBufferSize(VOID);
ULONG
Win32ConVideoGetBufferSize(VOID);

VOID
Win32VideoGetFontsFromFirmware(
    _Out_ PULONG RomFontPointers);
VOID
Win32ConVideoGetFontsFromFirmware(
    _Out_ PULONG RomFontPointers);

VOID
Win32VideoSetTextCursorPosition(
    _In_ UCHAR X,
    _In_ UCHAR Y);
VOID
Win32ConVideoSetTextCursorPosition(
    _In_ UCHAR X,
    _In_ UCHAR Y);

VOID
Win32VideoHideShowTextCursor(
    _In_ BOOLEAN Show);
VOID
Win32ConVideoHideShowTextCursor(
    _In_ BOOLEAN Show);

VOID
Win32VideoPutChar(
    _In_ int Ch,
    _In_ UCHAR Attr,
    _In_ unsigned X,
    _In_ unsigned Y);
VOID
Win32ConVideoPutChar(
    _In_ int Ch,
    _In_ UCHAR Attr,
    _In_ unsigned X,
    _In_ unsigned Y);


VOID
Win32VideoCopyOffScreenBufferToVRAM(
    _In_ PVOID Buffer);
VOID
Win32ConVideoCopyOffScreenBufferToVRAM(
    _In_ PVOID Buffer);

BOOLEAN
Win32VideoIsPaletteFixed(VOID);
BOOLEAN
Win32ConVideoIsPaletteFixed(VOID);

VOID
Win32VideoSetPaletteColor(
    _In_ UCHAR Color,
    _In_ UCHAR Red,
    _In_ UCHAR Green,
    _In_ UCHAR Blue);
VOID
Win32ConVideoSetPaletteColor(
    _In_ UCHAR Color,
    _In_ UCHAR Red,
    _In_ UCHAR Green,
    _In_ UCHAR Blue);

VOID
Win32VideoGetPaletteColor(
    _In_ UCHAR Color,
    _Out_ PUCHAR Red,
    _Out_ PUCHAR Green,
    _Out_ PUCHAR Blue);
VOID
Win32ConVideoGetPaletteColor(
    _In_ UCHAR Color,
    _Out_ PUCHAR Red,
    _Out_ PUCHAR Green,
    _Out_ PUCHAR Blue);

VOID
Win32VideoSync(VOID);
VOID
Win32ConVideoSync(VOID);


VOID
Win32Beep(VOID);

PFREELDR_MEMORY_DESCRIPTOR
Win32MemGetMemoryMap(
    _Out_ PULONG MemoryMapSize);

VOID
Win32GetExtendedBIOSData(
    _Out_ PULONG ExtendedBIOSDataArea,
    _Out_ PULONG ExtendedBIOSDataSize);

UCHAR
Win32GetFloppyCount(VOID);

BOOLEAN
Win32DiskReadLogicalSectors(
    _In_ UCHAR DriveNumber,
    _In_ ULONGLONG SectorNumber,
    _In_ ULONG SectorCount,
    _Out_ PVOID Buffer);

BOOLEAN
Win32DiskGetDriveGeometry(
    _In_ UCHAR DriveNumber,
    _Out_ PGEOMETRY Geometry);

ULONG
Win32DiskGetCacheableBlockCount(
    _In_ UCHAR DriveNumber);

TIMEINFO*
Win32GetTime(VOID);

BOOLEAN
Win32InitializeBootDevices(VOID);

PCONFIGURATION_COMPONENT_DATA
Win32HwDetect(
    _In_opt_ PCSTR Options);

VOID
Win32PrepareForReactOS(VOID);

VOID
Win32HwIdle(VOID);

VOID
Win32VideoScrollUp(VOID);
VOID
Win32ConVideoScrollUp(VOID);
