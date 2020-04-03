/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Tests for console history APIs.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

#include "precomp.h"

static BOOL s_bIsVistaPlus;

typedef BOOL (WINAPI *PFN_GET_CONSOLE_HISTORY_INFO)(IN PCONSOLE_HISTORY_INFO);

#if !defined(_WINCON_H) || (_WIN32_WINNT < _WIN32_WINNT_VISTA)
typedef struct _CONSOLE_HISTORY_INFO {
    UINT cbSize;
    UINT HistoryBufferSize;
    UINT NumberOfHistoryBuffers;
    DWORD dwFlags;
} CONSOLE_HISTORY_INFO, *PCONSOLE_HISTORY_INFO;
#endif

START_TEST(ConsoleHistory)
{
    OSVERSIONINFOW osver = { sizeof(osver) };
    BOOL Success;
    PFN_GET_CONSOLE_HISTORY_INFO pGetConsoleHistoryInfo;
    CONSOLE_HISTORY_INFO HistoryInfo;

    // GetVersionExW(&osver);
    RtlGetVersion((PRTL_OSVERSIONINFOW)&osver);
    s_bIsVistaPlus = (osver.dwMajorVersion > 6) ||
                     (osver.dwMajorVersion == 6 && osver.dwMinorVersion >= 0);

    FreeConsole();
    ok(AllocConsole(), "Couldn't alloc console\n");

    if (s_bIsVistaPlus)
    {
        pGetConsoleHistoryInfo = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetConsoleHistoryInfo");
        if (!pGetConsoleHistoryInfo)
            ok(FALSE, "Windows >= Vista but no kernel32!GetConsoleHistoryInfo() export?!\n");
        if (pGetConsoleHistoryInfo)
        {
            RtlZeroMemory(&HistoryInfo, sizeof(HistoryInfo));
            HistoryInfo.cbSize = sizeof(HistoryInfo);

            Success = pGetConsoleHistoryInfo(&HistoryInfo);
            ok(Success, "GetConsoleHistoryInfo() failed with error %lu\n", GetLastError());

            /*
             * When the console uses standard default settings the number
             * of history buffers should not be zero.
             */
            ok(HistoryInfo.NumberOfHistoryBuffers > 0, "NumberOfHistoryBuffers is zero!\n");
            trace("HistoryInfo.NumberOfHistoryBuffers = %ld\n", HistoryInfo.NumberOfHistoryBuffers);
        }
    }
    else
    {
        skip("GetConsoleHistoryInfo() testcase is skipped because Windows version is < Vista.\n");
    }
}
