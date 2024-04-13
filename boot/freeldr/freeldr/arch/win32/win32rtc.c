/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Real-time clock access routine for Win32
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <win32ldr.h>

/* FUNCTIONS ******************************************************************/

TIMEINFO*
Win32GetTime(VOID)
{
    static TIMEINFO TimeInfo;
    SYSTEMTIME time = {0};

    GetLocalTime(&time);

    TimeInfo.Year  = time.wYear;
    TimeInfo.Month = time.wMonth;
    TimeInfo.Day   = time.wDay;
    TimeInfo.Hour    = time.wHour;
    TimeInfo.Minutes = time.wMinute;
    TimeInfo.Seconds = time.wSecond;
    TimeInfo.Milliseconds = time.wMilliseconds;
    return &TimeInfo;
}

/* EOF */
