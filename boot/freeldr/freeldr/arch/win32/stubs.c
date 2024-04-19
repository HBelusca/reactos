/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Function stubs
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

VOID
StallExecutionProcessor(ULONG Microseconds)
{
}

PFREELDR_MEMORY_DESCRIPTOR
Win32MemGetMemoryMap(
    _Out_ PULONG MemoryMapSize)
{
    ERR("Win32MemGetMemoryMap(0x%p) is UNIMPLEMENTED\n", MemoryMapSize);
    return NULL;
}

VOID
Win32GetExtendedBIOSData(
    _Out_ PULONG ExtendedBIOSDataArea,
    _Out_ PULONG ExtendedBIOSDataSize)
{
    ERR("Win32GetExtendedBIOSData(0x%p, 0x%p) is UNIMPLEMENTED\n",
        ExtendedBIOSDataArea, ExtendedBIOSDataSize);
}

/* EOF */
