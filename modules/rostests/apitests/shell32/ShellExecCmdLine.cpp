/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test for ShellExecCmdLine
 * PROGRAMMERS:     Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "shelltest.h"
#include <shlwapi.h>
#include <strsafe.h>
#include <versionhelpers.h>
#include "shell32_apitest_sub.h"

#include <reactos/undocshell.h> // or "undocshell.h"; for SECL_***

#define NDEBUG
#include <debug.h>
#include <stdio.h>


#define ShellExecCmdLine proxy_ShellExecCmdLine

// For the dummy ShellExecCmdLine() implementation...
#define shell32_hInstance   GetModuleHandle(NULL)
#define IDS_FILE_NOT_FOUND  (-1)

static __inline void __SHCloneStrW(WCHAR **target, const WCHAR *source)
{
    *target = (WCHAR *)SHAlloc((lstrlenW(source) + 1) * sizeof(WCHAR) );
    lstrcpyW(*target, source);
}

HRESULT WINAPI ShellExecCmdLine(
    HWND hwnd,
    LPCWSTR pwszCommand,
    LPCWSTR pwszStartDir,
    int nShow,
    LPVOID pUnused,
    DWORD dwSeclFlags)
{
//
// TODO: Include shell32/shlexec.cpp implementation.
//
    return S_OK;
}

#undef ShellExecCmdLine


typedef HRESULT (WINAPI *SHELLEXECCMDLINE)(HWND, LPCWSTR, LPCWSTR, INT, LPVOID, DWORD);
SHELLEXECCMDLINE g_pShellExecCmdLine = NULL;

typedef struct TEST_ENTRY
{
    INT lineno;
    BOOL result;
    BOOL bAllowNonExe;
    LPCWSTR pwszCommand;
    LPCWSTR pwszStartDir;
} TEST_ENTRY;

static WCHAR s_sub_program[MAX_PATH];
static WCHAR s_win_test_exe[MAX_PATH];
static WCHAR s_sys_bat_file[MAX_PATH];
static WCHAR s_cur_dir[MAX_PATH];

// NOTE: See FindExecutable.cpp, ShellExecuteEx.cpp
static BOOL
GetSubProgramPath(void)
{
    GetModuleFileNameW(NULL, s_sub_program, _countof(s_sub_program));
    PathRemoveFileSpecW(s_sub_program);
    PathAppendW(s_sub_program, L"shell32_apitest_sub.exe");

    if (!PathFileExistsW(s_sub_program))
    {
        PathRemoveFileSpecW(s_sub_program);
        PathAppendW(s_sub_program, L"testdata\\shell32_apitest_sub.exe");

        if (!PathFileExistsW(s_sub_program))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static const TEST_ENTRY s_entries_1[] =
{
    // NULL
    { __LINE__, 0xBADFACE, FALSE, NULL, NULL },
    { __LINE__, 0xBADFACE, FALSE, NULL, L"." },
    { __LINE__, 0xBADFACE, FALSE, NULL, L"system32" },
    { __LINE__, 0xBADFACE, FALSE, NULL, L"C:\\Program Files" },
    { __LINE__, 0xBADFACE, TRUE, NULL, NULL },
    { __LINE__, 0xBADFACE, TRUE, NULL, L"." },
    { __LINE__, 0xBADFACE, TRUE, NULL, L"system32" },
    { __LINE__, 0xBADFACE, TRUE, NULL, L"C:\\Program Files" },
    // notepad
    { __LINE__, TRUE, FALSE, L"notepad", NULL },
    { __LINE__, TRUE, FALSE, L"notepad", L"." },
    { __LINE__, TRUE, FALSE, L"notepad", L"system32" },
    { __LINE__, TRUE, FALSE, L"notepad", L"C:\\Program Files" },
    { __LINE__, TRUE, FALSE, L"notepad \"Test File.txt\"", NULL },
    { __LINE__, TRUE, FALSE, L"notepad \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"notepad", NULL },
    { __LINE__, TRUE, TRUE, L"notepad", L"." },
    { __LINE__, TRUE, TRUE, L"notepad", L"system32" },
    { __LINE__, TRUE, TRUE, L"notepad", L"C:\\Program Files" },
    { __LINE__, TRUE, TRUE, L"notepad \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"notepad \"Test File.txt\"", L"." },
    // notepad.exe
    { __LINE__, TRUE, FALSE, L"notepad.exe", NULL },
    { __LINE__, TRUE, FALSE, L"notepad.exe", L"." },
    { __LINE__, TRUE, FALSE, L"notepad.exe", L"system32" },
    { __LINE__, TRUE, FALSE, L"notepad.exe", L"C:\\Program Files" },
    { __LINE__, TRUE, FALSE, L"notepad.exe \"Test File.txt\"", NULL },
    { __LINE__, TRUE, FALSE, L"notepad.exe \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"notepad.exe", NULL },
    { __LINE__, TRUE, TRUE, L"notepad.exe", L"." },
    { __LINE__, TRUE, TRUE, L"notepad.exe", L"system32" },
    { __LINE__, TRUE, TRUE, L"notepad.exe", L"C:\\Program Files" },
    { __LINE__, TRUE, TRUE, L"notepad.exe \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"notepad.exe \"Test File.txt\"", L"." },
    // C:\notepad.exe
    { __LINE__, FALSE, FALSE, L"C:\\notepad.exe", NULL },
    { __LINE__, FALSE, FALSE, L"C:\\notepad.exe", L"." },
    { __LINE__, FALSE, FALSE, L"C:\\notepad.exe", L"system32" },
    { __LINE__, FALSE, FALSE, L"C:\\notepad.exe", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"C:\\notepad.exe \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"C:\\notepad.exe \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"C:\\notepad.exe", NULL },
    { __LINE__, FALSE, TRUE, L"C:\\notepad.exe", L"." },
    { __LINE__, FALSE, TRUE, L"C:\\notepad.exe", L"system32" },
    { __LINE__, FALSE, TRUE, L"C:\\notepad.exe", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"C:\\notepad.exe \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"C:\\notepad.exe \"Test File.txt\"", L"." },
    // "notepad"
    { __LINE__, TRUE, FALSE, L"\"notepad\"", NULL },
    { __LINE__, TRUE, FALSE, L"\"notepad\"", L"." },
    { __LINE__, TRUE, FALSE, L"\"notepad\"", L"system32" },
    { __LINE__, TRUE, FALSE, L"\"notepad\"", L"C:\\Program Files" },
    { __LINE__, TRUE, FALSE, L"\"notepad\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, FALSE, L"\"notepad\" \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"notepad\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"notepad\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"notepad\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"notepad\"", L"C:\\Program Files" },
    { __LINE__, TRUE, TRUE, L"\"notepad\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"notepad\" \"Test File.txt\"", L"." },
    // "notepad.exe"
    { __LINE__, TRUE, FALSE, L"\"notepad.exe\"", NULL },
    { __LINE__, TRUE, FALSE, L"\"notepad.exe\"", L"." },
    { __LINE__, TRUE, FALSE, L"\"notepad.exe\"", L"system32" },
    { __LINE__, TRUE, FALSE, L"\"notepad.exe\"", L"C:\\Program Files" },
    { __LINE__, TRUE, FALSE, L"\"notepad.exe\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, FALSE, L"\"notepad.exe\" \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"notepad.exe\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"notepad.exe\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"notepad.exe\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"notepad.exe\"", L"C:\\Program Files" },
    { __LINE__, TRUE, TRUE, L"\"notepad.exe\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"notepad.exe\" \"Test File.txt\"", L"." },
    // test program
    { __LINE__, FALSE, FALSE, L"test program", NULL },
    { __LINE__, FALSE, FALSE, L"test program", L"." },
    { __LINE__, FALSE, FALSE, L"test program", L"system32" },
    { __LINE__, FALSE, FALSE, L"test program", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"test program \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"test program \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"test program", NULL },
    { __LINE__, FALSE, TRUE, L"test program", L"." },
    { __LINE__, FALSE, TRUE, L"test program", L"system32" },
    { __LINE__, FALSE, TRUE, L"test program", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"test program \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"test program \"Test File.txt\"", L"." },
    // test program.exe
    { __LINE__, FALSE, FALSE, L"test program.exe", NULL },
    { __LINE__, FALSE, FALSE, L"test program.exe", L"." },
    { __LINE__, FALSE, FALSE, L"test program.exe", L"system32" },
    { __LINE__, FALSE, FALSE, L"test program.exe", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"test program.exe \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"test program.exe \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"test program.exe", NULL },
    { __LINE__, FALSE, TRUE, L"test program.exe", L"." },
    { __LINE__, FALSE, TRUE, L"test program.exe", L"system32" },
    { __LINE__, FALSE, TRUE, L"test program.exe", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"test program.exe \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"test program.exe \"Test File.txt\"", L"." },
    // test program.bat
    { __LINE__, FALSE, FALSE, L"test program.bat", NULL },
    { __LINE__, FALSE, FALSE, L"test program.bat", L"." },
    { __LINE__, FALSE, FALSE, L"test program.bat", L"system32" },
    { __LINE__, FALSE, FALSE, L"test program.bat", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"test program.bat \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"test program.bat \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"test program.bat", NULL },
    { __LINE__, FALSE, TRUE, L"test program.bat", L"." },
    { __LINE__, FALSE, TRUE, L"test program.bat", L"system32" },
    { __LINE__, FALSE, TRUE, L"test program.bat", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"test program.bat \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"test program.bat \"Test File.txt\"", L"." },
    // "test program"
    { __LINE__, FALSE, FALSE, L"\"test program\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"test program\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"test program\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"test program\"", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"\"test program\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"test program\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"test program\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"test program\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"test program\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"test program\"", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"\"test program\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"test program\" \"Test File.txt\"", L"." },
    // "test program.exe"
    { __LINE__, TRUE, FALSE, L"\"test program.exe\"", NULL },
    { __LINE__, TRUE, FALSE, L"\"test program.exe\"", L"." },
    { __LINE__, TRUE, FALSE, L"\"test program.exe\"", L"system32" },
    { __LINE__, TRUE, FALSE, L"\"test program.exe\"", L"C:\\Program Files" },
    { __LINE__, TRUE, FALSE, L"\"test program.exe\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, FALSE, L"\"test program.exe\" \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"test program.exe\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"test program.exe\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"test program.exe\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"test program.exe\"", L"C:\\Program Files" },
    { __LINE__, TRUE, TRUE, L"\"test program.exe\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"test program.exe\" \"Test File.txt\"", L"." },
    // "test program.bat"
    { __LINE__, FALSE, FALSE, L"\"test program.bat\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"test program.bat\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"test program.bat\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"test program.bat\"", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"\"test program.bat\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"test program.bat\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"test program.bat\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"test program.bat\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"test program.bat\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"test program.bat\"", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"\"test program.bat\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"test program.bat\" \"Test File.txt\"", L"." },
    // invalid program
    { __LINE__, FALSE, FALSE, L"invalid program", NULL },
    { __LINE__, FALSE, FALSE, L"invalid program", L"." },
    { __LINE__, FALSE, FALSE, L"invalid program", L"system32" },
    { __LINE__, FALSE, FALSE, L"invalid program", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"invalid program \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"invalid program \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"invalid program", NULL },
    { __LINE__, FALSE, TRUE, L"invalid program", L"." },
    { __LINE__, FALSE, TRUE, L"invalid program", L"system32" },
    { __LINE__, FALSE, TRUE, L"invalid program", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"invalid program \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"invalid program \"Test File.txt\"", L"." },
    // \"invalid program.exe\"
    { __LINE__, FALSE, FALSE, L"\"invalid program.exe\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"invalid program.exe\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"invalid program.exe\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"invalid program.exe\"", L"C:\\Program Files" },
    { __LINE__, FALSE, FALSE, L"\"invalid program.exe\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"invalid program.exe\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"invalid program.exe\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"invalid program.exe\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"invalid program.exe\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"invalid program.exe\"", L"C:\\Program Files" },
    { __LINE__, FALSE, TRUE, L"\"invalid program.exe\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"invalid program.exe\" \"Test File.txt\"", L"." },
    // My Documents
    { __LINE__, TRUE, TRUE, L"::{450d8fba-ad25-11d0-98a8-0800361b1103}", NULL },
    { __LINE__, TRUE, TRUE, L"shell:::{450d8fba-ad25-11d0-98a8-0800361b1103}", NULL },
    // shell:sendto
    { __LINE__, TRUE, TRUE, L"shell:sendto", NULL },
    // iexplore.exe
    { __LINE__, TRUE, FALSE, L"iexplore", NULL },
    { __LINE__, TRUE, FALSE, L"iexplore.exe", NULL },
    { __LINE__, TRUE, TRUE, L"iexplore", NULL },
    { __LINE__, TRUE, TRUE, L"iexplore.exe", NULL },
    // https://google.com
    { __LINE__, TRUE, FALSE, L"https://google.com", NULL },
    { __LINE__, TRUE, TRUE, L"https://google.com", NULL },
    // Test File 1.txt
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", NULL },
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", L"." },
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", L"system32" },
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"Test File 1.txt", NULL },
    { __LINE__, TRUE, TRUE, L"Test File 1.txt", L"." },
    { __LINE__, FALSE, TRUE, L"Test File 1.txt", L"system32" },
    { __LINE__, TRUE, TRUE, L"Test File 1.txt", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"\"Test File 1.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", s_cur_dir },
    // Test File 2.bat
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", NULL },
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", L"." },
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", L"system32" },
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", NULL },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", L"." },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", L"system32" },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", s_cur_dir },
};

static const TEST_ENTRY s_entries_2[] =
{
    // Test File 1.txt (with setting path)
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", NULL },
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", L"." },
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", L"system32" },
    { __LINE__, FALSE, FALSE, L"Test File 1.txt", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\"", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 1.txt\" \"Test File.txt\"", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"Test File 1.txt", NULL },
    { __LINE__, TRUE, TRUE, L"Test File 1.txt", L"." },
    { __LINE__, FALSE, TRUE, L"Test File 1.txt", L"system32" },
    { __LINE__, TRUE, TRUE, L"Test File 1.txt", s_cur_dir },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\"", s_cur_dir },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", NULL },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", L"." },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", L"system32" },
    { __LINE__, TRUE, TRUE, L"\"Test File 1.txt\" \"Test File.txt\"", s_cur_dir },
    // Test File 2.bat (with setting path)
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", NULL },
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", L"." },
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", L"system32" },
    { __LINE__, FALSE, FALSE, L"Test File 2.bat", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\"", s_cur_dir },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", L"system32" },
    { __LINE__, FALSE, FALSE, L"\"Test File 2.bat\" \"Test File.txt\"", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", NULL },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", L"." },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", L"system32" },
    { __LINE__, FALSE, TRUE, L"Test File 2.bat", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\"", s_cur_dir },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", NULL },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", L"." },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", L"system32" },
    { __LINE__, FALSE, TRUE, L"\"Test File 2.bat\" \"Test File.txt\"", s_cur_dir },
};

typedef struct OPENWNDS
{
    UINT count;
    HWND *phwnd;
} OPENWNDS;

static OPENWNDS s_wi0 = { 0 }, s_wi1 = { 0 };

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    OPENWNDS *info = (OPENWNDS *)lParam;
    info->phwnd = (HWND *)realloc(info->phwnd, (info->count + 1) * sizeof(HWND));
    if (!info->phwnd)
        return FALSE;
    info->phwnd[info->count] = hwnd;
    ++(info->count);
    return TRUE;
}

static void CleanupNewlyCreatedWindows(void)
{
    EnumWindows(EnumWindowsProc, (LPARAM)&s_wi1);
    for (UINT i1 = 0; i1 < s_wi1.count; ++i1)
    {
        BOOL bFound = FALSE;
        for (UINT i0 = 0; i0 < s_wi0.count; ++i0)
        {
            if (s_wi1.phwnd[i1] == s_wi0.phwnd[i0])
            {
                bFound = TRUE;
                break;
            }
        }
        if (!bFound)
            PostMessageW(s_wi1.phwnd[i1], WM_CLOSE, 0, 0);
    }
    free(s_wi1.phwnd);
    ZeroMemory(&s_wi1, sizeof(s_wi1));
}

static void DoEntry(const TEST_ENTRY *pEntry)
{
    HRESULT hr;
    DWORD dwSeclFlags;
    BOOL result;

    if (pEntry->bAllowNonExe)
        dwSeclFlags = SECL_NO_UI | SECL_USERCMD_PARSE_UNSAFE;
    else
        dwSeclFlags = SECL_NO_UI;

    _SEH2_TRY
    {
        if (IsReactOS())
        {
            hr = proxy_ShellExecCmdLine(NULL, pEntry->pwszCommand, pEntry->pwszStartDir,
                                        SW_SHOWNORMAL, NULL, dwSeclFlags);
        }
        else
        {
            hr = (*g_pShellExecCmdLine)(NULL, pEntry->pwszCommand, pEntry->pwszStartDir,
                                        SW_SHOWNORMAL, NULL, dwSeclFlags);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        hr = 0xBADFACE;
    }
    _SEH2_END;

    if (hr == 0xBADFACE)
        result = hr;
    else
        result = (hr == S_OK);

    ok(result == pEntry->result, "Line %d: result expected %d, was %d\n",
       pEntry->lineno, pEntry->result, result);

    CleanupNewlyCreatedWindows();
}

START_TEST(ShellExecCmdLine)
{
    using namespace std;

    if (!IsReactOS())
    {
        if (!IsWindowsVistaOrGreater())
        {
            skip("ShellExecCmdLine is not available on this platform\n");
            return;
        }

        HMODULE hShell32 = GetModuleHandleA("shell32");
        g_pShellExecCmdLine = (SHELLEXECCMDLINE)GetProcAddress(hShell32, (LPCSTR)(INT_PTR)265);
        if (!g_pShellExecCmdLine)
        {
            skip("ShellExecCmdLine is not found\n");
            return;
        }
    }

    if (!GetSubProgramPath())
    {
        skip("shell32_apitest_sub.exe is not found\n");
        return;
    }

    // record open windows
    if (!EnumWindows(EnumWindowsProc, (LPARAM)&s_wi0))
    {
        skip("EnumWindows failed\n");
        free(s_wi0.phwnd);
        return;
    }

    // s_win_test_exe
    GetWindowsDirectoryW(s_win_test_exe, _countof(s_win_test_exe));
    PathAppendW(s_win_test_exe, L"test program.exe");
    BOOL ret = CopyFileW(s_sub_program, s_win_test_exe, FALSE);
    if (!ret)
    {
        skip("Please retry with admin rights\n");
        free(s_wi0.phwnd);
        return;
    }

    FILE *fp;

    // s_sys_bat_file
    GetSystemDirectoryW(s_sys_bat_file, _countof(s_sys_bat_file));
    PathAppendW(s_sys_bat_file, L"test program.bat");
    fp = _wfopen(s_sys_bat_file, L"wb");
    fclose(fp);
    ok_int(PathFileExistsW(s_sys_bat_file), TRUE);

    // "Test File 1.txt"
    fp = fopen("Test File 1.txt", "wb");
    ok(fp != NULL, "failed to create a test file\n");
    fclose(fp);
    ok_int(PathFileExistsA("Test File 1.txt"), TRUE);

    // "Test File 2.bat"
    fp = fopen("Test File 2.bat", "wb");
    ok(fp != NULL, "failed to create a test file\n");
    fclose(fp);
    ok_int(PathFileExistsA("Test File 2.bat"), TRUE);

    // s_cur_dir
    GetCurrentDirectoryW(_countof(s_cur_dir), s_cur_dir);

    // do tests
    for (size_t i = 0; i < _countof(s_entries_1); ++i)
    {
        DoEntry(&s_entries_1[i]);
    }
    SetEnvironmentVariableW(L"PATH", s_cur_dir);
    for (size_t i = 0; i < _countof(s_entries_2); ++i)
    {
        DoEntry(&s_entries_2[i]);
    }

    Sleep(2000);
    CleanupNewlyCreatedWindows();

    // clean up
    ok(DeleteFileW(s_win_test_exe), "failed to delete the test file\n");
    ok(DeleteFileW(s_sys_bat_file), "failed to delete the test file\n");
    ok(DeleteFileA("Test File 1.txt"), "failed to delete the test file\n");
    ok(DeleteFileA("Test File 2.bat"), "failed to delete the test file\n");
    free(s_wi0.phwnd);

    DoWaitForWindow(CLASSNAME, CLASSNAME, TRUE, TRUE);
    Sleep(100);
}
