/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Utilities Library
 * FILE:            sdk/lib/conutils/history.c
 * PURPOSE:         Text-line History and Aliases management for Console applications.
 * PROGRAMMERS:     Hermes Belusca-Maito
 */

/* FIXME: Temporary HACK before we cleanly support UNICODE functions */
#define UNICODE
#define _UNICODE

/**/ // FIXME!
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
/**/

#include <windef.h>
#include <winbase.h>
// #include <winnls.h>
#include <wincon.h>  // Console APIs (only if kernel32 support included)
// #include <strsafe.h>

// #include "conutils.h"
#include "history.h"


static BOOL
OpenConsoleHandles(
    OUT PHANDLE phConsoleInput,
    OUT PHANDLE phConsoleOutput)
{
    *phConsoleInput  = CreateFile(_T("CONIN$"),
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL,
                                  OPEN_EXISTING,
                                  0, NULL);
    if (!*phConsoleInput)
        return FALSE;

    *phConsoleOutput = CreateFile(_T("CONOUT$"),
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL,
                                  OPEN_EXISTING,
                                  0, NULL);
    if (!*phConsoleOutput)
    {
        CloseHandle(*phConsoleInput);
        return FALSE;
    }

    return TRUE;
}

static VOID
CloseConsoleHandles(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput)
{
    CloseHandle(hConsoleOutput);
    CloseHandle(hConsoleInput);
}

static BOOL
CaptureConsoleInputExeName(
    IN OUT LPCTSTR* pExeName,
    IN OUT PTCHAR CaptureBuffer,
    IN DWORD BufferLength)
{
    LPCTSTR ExeName;

    /* Parameters must be valid */
    if (!pExeName || !CaptureBuffer || BufferLength == 0)
        return FALSE;

    ExeName = *pExeName;
    BufferLength = min(BufferLength, EXENAME_LENGTH);

    /* If a console input executable name has been specified, use it */
    if (ExeName && *ExeName)
    {
        /* Check whether it's too long, and if so, keep only the last characters */
        DWORD dwLength = _tcslen(ExeName) + 1;
#if 0
        if (dwLength > BufferLength)
            ExeName += (dwLength - BufferLength);

        /* Capture it in the buffer */
        _tcscpy(CaptureBuffer, ExeName);
#else
        /* We can actually just re-use the user-provided pointer */
        if (dwLength > EXENAME_LENGTH)
            ExeName += (dwLength - EXENAME_LENGTH);

        CaptureBuffer = (PTCHAR)ExeName;
#endif
    }
    /* Otherwise, retrieve the current active input console application name */
    else
    {
        /* The console application name is NULL-terminated */
        *CaptureBuffer = _T('\0'); // It also has to be initialized to NULL before.
        GetConsoleInputExeName(BufferLength, CaptureBuffer);
    }

    /* Return the pointer value */
    *pExeName = CaptureBuffer;
    return TRUE;
}

static VOID
HistorySetupConsole(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    IN LPCTSTR ExeName OPTIONAL,
    OUT LPTSTR pOrgExeName OPTIONAL,
    IN DWORD nOrgExeNameLen,
    OUT PDWORD pdwInputMode,
    OUT PDWORD pdwOutputMode,
    OUT PCOORD pOrgCursorPos,
    OUT PCOORD pCurrentCursorPos,
    OUT PTCHAR pOrgChar)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD dwRead;

    /* Now use ReadConsole() to "read" the line that has been entered so that it will add it to the history */
    GetConsoleMode(hConsoleInput, pdwInputMode);
    GetConsoleMode(hConsoleOutput, pdwOutputMode);

    /*
     * ENABLE_LINE_INPUT is needed to enable line-editing capabilities (line discipline), and ENABLE_ECHO_INPUT is needed in addition
     * so that ReadConsole() can store the line in its history. This is due to the fact ReadConsole() only stores input in history
     * only when ENABLE_ECHO_INPUT is enabled, this ensuring that anything sent to the console when echo is disabled (e.g.
     * binary data, or secrets like passwords...) does not remain stored in memory.
     */
    SetConsoleMode(hConsoleInput, (*pdwInputMode & ~(ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE |
                                                     ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT))
                                                 |  (ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    /*
     * However, we do not want to see the strings that are going to be read by ReadConsole()!
     * If we changed the active screenbuffer to something else, we would still see them.
     * To work around this problem, we instead disable line wrapping, move the cursor to the far right
     * of the console and then perform the read, so that each echoed character remains at the end.
     * Then we restore the original character that was at the end of the line.
     */
    SetConsoleMode(hConsoleOutput, *pdwOutputMode & ~(ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT));
    GetConsoleScreenBufferInfo(hConsoleOutput, &csbi);
    *pCurrentCursorPos = *pOrgCursorPos = csbi.dwCursorPosition;
    pCurrentCursorPos->X = csbi.dwSize.X-1;
    SetConsoleCursorPosition(hConsoleOutput, *pCurrentCursorPos);

    /* Save the character at the end of the line */
    dwRead = 0;
    ReadConsoleOutputCharacter(hConsoleOutput, pOrgChar, 1,
                               *pCurrentCursorPos, &dwRead);

    /* If a console input executable name has been specified, change it and return the original one */
    if (pOrgExeName && nOrgExeNameLen > 0)
        *pOrgExeName = _T('\0');
    if (ExeName && *ExeName)
    {
        /* Save the original console input executable name */
        if (pOrgExeName)
        {
            /* The console application name is NULL-terminated */
            nOrgExeNameLen = min(nOrgExeNameLen, EXENAME_LENGTH);
            if (nOrgExeNameLen > 0)
            {
                *pOrgExeName = _T('\0'); // It also has to be initialized to NULL before.
                GetConsoleInputExeName(nOrgExeNameLen, pOrgExeName);
            }
        }

        /* Check whether it's too long, and if so, keep only the last characters */
        dwRead = _tcslen(ExeName) + 1;
        if (dwRead > EXENAME_LENGTH)
            ExeName += (dwRead - EXENAME_LENGTH);
        SetConsoleInputExeName(ExeName);
    }
}

static VOID
HistoryRestoreConsole(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    IN LPCTSTR ExeName OPTIONAL,
    IN DWORD dwInputMode,
    IN DWORD dwOutputMode,
    IN COORD OrgCursorPos,
    IN COORD CurrentCursorPos,
    IN TCHAR OrgChar)
{
    DWORD dwWritten;

    /* If a console input executable name has been specified, restore it */
    if (ExeName && *ExeName)
    {
        /* Check whether it's too long, and if so, keep only the last characters */
        dwWritten = _tcslen(ExeName) + 1;
        if (dwWritten > EXENAME_LENGTH)
            ExeName += (dwWritten - EXENAME_LENGTH);
        SetConsoleInputExeName(ExeName);
    }

    /* Restore the character that was at the end of the line */
    dwWritten = 0;
    WriteConsoleOutputCharacter(hConsoleOutput, &OrgChar, 1,
                                CurrentCursorPos, &dwWritten);

    /* Move the cursor back at its original position */
    SetConsoleCursorPosition(hConsoleOutput, OrgCursorPos);

    /* Restore the original console modes */
    SetConsoleMode(hConsoleOutput, dwOutputMode);
    SetConsoleMode(hConsoleInput, dwInputMode);
}

static BOOL
ConAddToHistoryExWorker(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    IN LPCTSTR Line)
{
    DWORD dwLength;
    DWORD dwWritten;
    INPUT_RECORD Ir;
    PINPUT_RECORD InputRecords, pIr;
    // WORD VkKey; // MAKEWORD(low = vkey_code, high = shift_state);
    TCHAR TempBuffer[1024];

    if (!Line || !*Line)
        return TRUE;

    dwLength = _tcslen(Line);


    /* Prepare the common data */
    Ir.EventType = KEY_EVENT;
    Ir.Event.KeyEvent.wRepeatCount = 1;
    Ir.Event.KeyEvent.dwControlKeyState = 0;

    /* Initialize the key presses */
    dwWritten = (dwLength + 1) * 2; // + 1 for '\r', and * 2 for key down and up.
    InputRecords = (PINPUT_RECORD)HeapAlloc(GetProcessHeap(), 0,
                                            dwWritten * sizeof(*InputRecords));

    pIr = InputRecords;

    /* Simulate entering the string line by pressing keys */
    while (*Line)
    {
#ifdef _UNICODE
        Ir.Event.KeyEvent.uChar.UnicodeChar = *Line;
        // VkKey = VkKeyScanW(*Line);
#else
        Ir.Event.KeyEvent.uChar.AsciiChar = *Line;
        // VkKey = VkKeyScanA(*Line);
#endif
        Ir.Event.KeyEvent.wVirtualKeyCode  = 0; // LOBYTE(VkKey);
        Ir.Event.KeyEvent.wVirtualScanCode = 0; // MapVirtualKeyW(LOBYTE(VkKey), MAPVK_VK_TO_VSC);

        Ir.Event.KeyEvent.bKeyDown = TRUE;
        *pIr++ = Ir;

        Ir.Event.KeyEvent.bKeyDown = FALSE;
        *pIr++ = Ir;

        ++Line;
    }

    /* Simulate pressing ENTER */
#ifdef _UNICODE
    Ir.Event.KeyEvent.uChar.UnicodeChar = _T('\r');
    // VkKey = VkKeyScanW(_T('\r'));
#else
    Ir.Event.KeyEvent.uChar.AsciiChar = _T('\r');
    // VkKey = VkKeyScanA(_T('\r'));
#endif
    Ir.Event.KeyEvent.wVirtualKeyCode  = 0; // LOBYTE(VkKey);
    Ir.Event.KeyEvent.wVirtualScanCode = 0; // MapVirtualKeyW(LOBYTE(VkKey), MAPVK_VK_TO_VSC);

    Ir.Event.KeyEvent.bKeyDown = TRUE;
    *pIr++ = Ir;

    Ir.Event.KeyEvent.bKeyDown = FALSE;
    *pIr = Ir;

    WriteConsoleInput(hConsoleInput, InputRecords, dwWritten, &dwWritten);

    HeapFree(GetProcessHeap(), 0, InputRecords);


    /* Now use ReadConsole() to "read" the line that has been entered so that it will add it to the history */

    ++dwLength; // Take the '\r' into account.
    while (dwLength)
    {
        /* Read the line by chunks */
        dwWritten = min(dwLength, ARRAYSIZE(TempBuffer));
        dwLength -= dwWritten;
        ReadConsole(hConsoleInput,
                    TempBuffer, dwWritten,
                    &dwWritten, NULL);
    }
    FlushConsoleInputBuffer(hConsoleInput);

    return TRUE;
}

BOOL
ConAddToHistoryEx(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR Line)
{
    BOOL Success;
    DWORD dwInputMode, dwOutputMode;
    COORD OrgCursorPos, CurrentCursorPos;
    TCHAR OrgChar;
    TCHAR OrgExeName[EXENAME_LENGTH];

    /* Configure the console for setting history */
    HistorySetupConsole(hConsoleInput, hConsoleOutput,
                        ExeName, OrgExeName, ARRAYSIZE(OrgExeName),
                        &dwInputMode, &dwOutputMode,
                        &OrgCursorPos, &CurrentCursorPos,
                        &OrgChar);

    /* Call the worker function */
    Success = ConAddToHistoryExWorker(hConsoleInput, hConsoleOutput, Line);

    /* Restore the console to its previous state */
    HistoryRestoreConsole(hConsoleInput, hConsoleOutput,
                          OrgExeName,
                          dwInputMode, dwOutputMode,
                          OrgCursorPos, CurrentCursorPos,
                          OrgChar);

    return Success;
}

BOOL
ConAddToHistory(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR Line)
{
    BOOL Success;
    HANDLE hConsoleInput, hConsoleOutput;

    /* Get the active console input and screenbuffer handles */
    if (!OpenConsoleHandles(&hConsoleInput, &hConsoleOutput))
        return FALSE;

    /* Call the helper function */
    Success = ConAddToHistoryEx(hConsoleInput, hConsoleOutput, ExeName, Line);

    /* Close the console handles */
    CloseConsoleHandles(hConsoleInput, hConsoleOutput);

    return Success;
}

BOOL
ConLoadHistory(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName)
{
    HANDLE hConsoleInput, hConsoleOutput;
    DWORD dwInputMode, dwOutputMode;
    COORD OrgCursorPos, CurrentCursorPos;
    TCHAR OrgChar;
    TCHAR OrgExeName[EXENAME_LENGTH];

    FILE* fp;
    WCHAR Line[MAX_PATH];

    /* Open the history file */
    fp = _tfopen(FileName, _T("rt"));
    if (!fp)
    {
        _tperror(FileName);
        return FALSE;
    }

    /* Get the active console input and screenbuffer handles */
    if (!OpenConsoleHandles(&hConsoleInput, &hConsoleOutput))
    {
        fclose(fp);
        return FALSE;
    }

    /* Configure the console for setting history */
    HistorySetupConsole(hConsoleInput, hConsoleOutput,
                        ExeName, OrgExeName, ARRAYSIZE(OrgExeName),
                        &dwInputMode, &dwOutputMode,
                        &OrgCursorPos, &CurrentCursorPos,
                        &OrgChar);

    while (_fgetts(Line, ARRAYSIZE(Line), fp) != NULL)
    {
        /* Remove trailing newline character */
        PTCHAR end = &Line[_tcslen(Line) - 1];
        if (*end == _T('\n'))
            *end = _T('\0');

        if (*Line)
        {
            /* Call the worker function */
            ConAddToHistoryExWorker(hConsoleInput, hConsoleOutput, Line);
        }
    }

    /* Restore the console to its previous state */
    HistoryRestoreConsole(hConsoleInput, hConsoleOutput,
                          OrgExeName,
                          dwInputMode, dwOutputMode,
                          OrgCursorPos, CurrentCursorPos,
                          OrgChar);

    /* Close the console handles */
    CloseConsoleHandles(hConsoleInput, hConsoleOutput);

    /* Close the history file */
    fclose(fp);

    return TRUE;
}

BOOL
ConGetHistoryBuffer(
    IN LPCTSTR ExeName OPTIONAL,
    OUT LPTSTR* lpHistory,
    OUT PDWORD pcbHistory)
{
    LPTSTR History;
    DWORD cbHistory;

    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return FALSE;
    }

    /* Initialize the values to be returned */
    *lpHistory  = NULL;
    *pcbHistory = 0;

    /* Retrieve the length of the history (in bytes) */
    cbHistory = GetConsoleCommandHistoryLength(ExeName);
    if (cbHistory == 0)
        return FALSE;

    /* Allocate the history buffer */
    History = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbHistory);
    if (!History)
        return FALSE;

    /* Retrieve the history */
    cbHistory = GetConsoleCommandHistory(History, cbHistory, ExeName);
    if (cbHistory == 0)
    {
        /* An error happened, free the allocated buffer and return failure */
        HeapFree(GetProcessHeap(), 0, History);
        return FALSE;
    }

    /* Return the history, and success */
    *lpHistory  = History;
    *pcbHistory = cbHistory;
    return TRUE;
}

BOOL
ConSaveHistory(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName)
{
    BOOL Success = FALSE;
    LPTSTR History;
    DWORD cbHistory;

    FILE* fp;

    /* Open the history file */
    fp = _tfopen(FileName, _T("wt"));
    if (!fp)
    {
        _tperror(FileName);
        return FALSE;
    }

    /* Retrieve the history */
    if (ConGetHistoryBuffer(ExeName, &History, &cbHistory))
    {
        LPTSTR Hist, HistEnd;

        Hist = History;
        HistEnd = (LPTSTR)((ULONG_PTR)History + cbHistory);

        /* Dump it to the file */
        for (; Hist < HistEnd; Hist += _tcslen(Hist) + 1)
        {
            _ftprintf(fp, _T("%s\n"), Hist);
        }

        HeapFree(GetProcessHeap(), 0, History);

        Success = TRUE;
    }

    /* Close the history file */
    fclose(fp);

    return Success;
}

BOOL
ConSetHistorySize(
    IN LPCTSTR ExeName OPTIONAL,
    IN DWORD dwSize)
{
    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return FALSE;
    }

    /* Call the API */
    return SetConsoleNumberOfCommands(dwSize, ExeName);
}

#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)

BOOL
ConGetHistoryInfo(
    OUT PUINT pHistoryBufferSize,
    OUT PUINT pNumberOfHistoryBuffers,
    OUT PDWORD pdwFlags)
{
    CONSOLE_HISTORY_INFO chi = {sizeof(chi)};

    /* Call the API */
    if (!GetConsoleHistoryInfo(&chi))
        return FALSE;

    /* Return the information */
    *pHistoryBufferSize = chi.HistoryBufferSize;
    *pNumberOfHistoryBuffers = chi.NumberOfHistoryBuffers;
    *pdwFlags = chi.dwFlags;

    return TRUE;
}

BOOL
ConSetHistoryInfo(
    IN UINT HistoryBufferSize,
    IN UINT NumberOfHistoryBuffers,
    IN DWORD dwFlags)
{
    CONSOLE_HISTORY_INFO chi;

    chi.cbSize = sizeof(chi);
    chi.HistoryBufferSize = HistoryBufferSize;
    chi.NumberOfHistoryBuffers = NumberOfHistoryBuffers;
    chi.dwFlags = dwFlags;

    /* Call the API */
    return SetConsoleHistoryInfo(&chi);
}

#endif

VOID
ConClearHistory(
    IN LPCTSTR ExeName OPTIONAL)
{
    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return;
    }

    /* Call the API */
    ExpungeConsoleCommandHistory(ExeName);
}



#if 0

VOID ConInitHistory();

BOOL ConAddToHistory(Index, Line);

BOOL ConGetFromHistory(Index, Line);

VOID ConSyncHistoryWithConsole();

#endif




BOOL
ConAddAlias(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR Source,
    IN LPCTSTR Target)
{
    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return FALSE;
    }

    /* Call the API */
    return AddConsoleAlias(Source, Target, ExeName);
}

DWORD
ConGetAlias(
    IN LPCTSTR ExeName OPTIONAL,
    IN  LPCTSTR Source,
    OUT LPTSTR  TargetBuffer,
    IN  DWORD   TargetBufferLength)
{
    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return 0;
    }

    /* Call the API */
    return GetConsoleAlias(Source, TargetBuffer, TargetBufferLength, ExeName);
}

BOOL
ConGetAliasesList(
    IN LPCTSTR ExeName OPTIONAL,
    OUT LPTSTR* lpAliasesList,
    OUT PDWORD  pcbAliasesListLength)
{
    LPTSTR AliasesList;
    DWORD cbAliasesList;

    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return FALSE;
    }

    /* Initialize the values to be returned */
    *lpAliasesList = NULL;
    *pcbAliasesListLength = 0;

    /* Retrieve the length of the alias list (in bytes) */
    cbAliasesList = GetConsoleAliasesLength(ExeName);
    if (cbAliasesList == 0)
        return FALSE;

    /* Allocate the alias list buffer */
    AliasesList = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbAliasesList);
    if (!AliasesList)
        return FALSE;

    /* Retrieve the alias list */
    cbAliasesList = GetConsoleAliases(AliasesList, cbAliasesList, ExeName);
    if (cbAliasesList == 0)
    {
        /* An error happened, free the allocated buffer and return failure */
        HeapFree(GetProcessHeap(), 0, AliasesList);
        return FALSE;
    }

    /* Return the alias list, and success */
    *lpAliasesList = AliasesList;
    *pcbAliasesListLength = cbAliasesList;
    return TRUE;
}

BOOL
ConGetAliasExesList(
    OUT LPTSTR* lpExeNameList,
    OUT PDWORD  pcbExeNameListLength)
{
    LPTSTR ExeNameList;
    DWORD cbExeNameListLength;

    /* Initialize the values to be returned */
    *lpExeNameList = NULL;
    *pcbExeNameListLength = 0;

    /* Retrieve the length of the alias list (in bytes) */
    cbExeNameListLength = GetConsoleAliasExesLength();
    if (cbExeNameListLength == 0)
        return FALSE;

    /* Allocate the alias list buffer */
    ExeNameList = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbExeNameListLength);
    if (!ExeNameList)
        return FALSE;

    /* Retrieve the alias list */
    cbExeNameListLength = GetConsoleAliasExes(ExeNameList, cbExeNameListLength);
    if (cbExeNameListLength == 0)
    {
        /* An error happened, free the allocated buffer and return failure */
        HeapFree(GetProcessHeap(), 0, ExeNameList);
        return FALSE;
    }

    /* Return the alias list, and success */
    *lpExeNameList = ExeNameList;
    *pcbExeNameListLength = cbExeNameListLength;
    return TRUE;
}

VOID
ConClearAliases(
    IN LPCTSTR ExeName OPTIONAL)
{
    LPTSTR AliasesList;
    DWORD cbAliasesList;
    LPTSTR Alias, AliasesEnd, Separator;

    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        return;
    }

    /* Retrieve the list of aliases */
    if (!ConGetAliasesList(ExeName, &AliasesList, &cbAliasesList))
        return;

    Alias = AliasesList;
    AliasesEnd = (LPTSTR)((ULONG_PTR)AliasesList + cbAliasesList);

    /* Reset each alias, thus removing it effectively */
    for (; Alias < AliasesEnd; Alias += _tcslen(Alias) + 1)
    {
        /* Find the '=' separator and NULL it */
        Separator = _tcschr(Alias, _T('='));
        if (Separator) *Separator = _T('\0');

        /* Call the API */
        AddConsoleAlias(Alias, NULL, ExeName);

        /* Restore the '=' and proceed to the next alias */
        if (Separator) *Separator = _T('=');
    }

    /* Cleanup the list */
    HeapFree(GetProcessHeap(), 0, AliasesList);
}

BOOL
ConLoadAliases(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName)
{
    LPTSTR Source, Target;
    FILE* fp;
    WCHAR Line[MAX_PATH];

    TCHAR ExeNameBuffer[EXENAME_LENGTH];

    /* Open the history file */
    fp = _tfopen(FileName, _T("rt"));
    if (!fp)
    {
        _tperror(FileName);
        return FALSE;
    }

    /* Capture the console input executable name */
    if (!CaptureConsoleInputExeName(&ExeName,
                                    ExeNameBuffer,
                                    ARRAYSIZE(ExeNameBuffer)))
    {
        fclose(fp);
        return FALSE;
    }

    while (_fgetts(Line, ARRAYSIZE(Line), fp) != NULL)
    {
        /* Remove trailing newline character */
        PTCHAR end = &Line[_tcslen(Line) - 1];
        if (*end == _T('\n'))
            *end = _T('\0');

        if (*Line)
        {
            Source = Line;
            Target = _tcschr(Line, _T('='));
            if (Target)
            {
                *Target++ = _T('\0');
                if (!*Target)
                    Target = NULL;
            }

            /* Call the API */
            AddConsoleAlias(Source, Target, ExeName);
        }
    }

    /* Close the history file */
    fclose(fp);

    return TRUE;
}

BOOL
ConSaveAliases(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName)
{
    BOOL Success = FALSE;
    LPTSTR AliasesList;
    DWORD cbAliasesList;

    FILE* fp;

    /* Open the alias list file */
    fp = _tfopen(FileName, _T("wt"));
    if (!fp)
    {
        _tperror(FileName);
        return FALSE;
    }

    /* Retrieve the alias list */
    // /* Retrieve the list of aliases */
    if (ConGetAliasesList(ExeName, &AliasesList, &cbAliasesList))
    {
        LPTSTR Alias, AliasesEnd;

        Alias = AliasesList;
        AliasesEnd = (LPTSTR)((ULONG_PTR)AliasesList + cbAliasesList);

        /* Dump it to the file */
        for (; Alias < AliasesEnd; Alias += _tcslen(Alias) + 1)
        {
            /* The alias is already in the format "Source=Target" */
            _ftprintf(fp, _T("%s\n"), Alias);
        }

        /* Cleanup the list */
        HeapFree(GetProcessHeap(), 0, AliasesList);

        Success = TRUE;
    }

    /* Close the alias list file */
    fclose(fp);

    return Success;
}
