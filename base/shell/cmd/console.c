/*
 *  CONSOLE.C - console input/output functions.
 *
 *
 *  History:
 *
 *    20-Jan-1999 (Eric Kohl)
 *        started
 *
 *    03-Apr-2005 (Magnus Olsen <magnus@greatlord.com>)
 *        Remove all hardcoded strings in En.rc
 *
 *    01-Jul-2005 (Brandon Turner <turnerb7@msu.edu>)
 *        Added ConPrintfPaging and ConOutPrintfPaging
 *
 *    02-Feb-2007 (Paolo Devoti <devotip at gmail.com>)
 *        Fixed ConPrintfPaging
 */

#include "precomp.h"

#define OUTPUT_BUFFER_SIZE  4096

/* Cache codepage for text streams */
UINT InputCodePage;
UINT OutputCodePage;

/* Global console Screen and Pager */
CON_SCREEN StdOutScreen = INIT_CON_SCREEN(StdOut);
CON_PAGER  StdOutPager  = INIT_CON_PAGER(&StdOutScreen);


/********************* Console STREAM IN utility functions ********************/

// Used in misc.c prompt functions
VOID ConInDisable(VOID)
{
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode;

    GetConsoleMode(hInput, &dwMode);
    dwMode &= ~ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hInput, dwMode);
}

// Used in misc.c prompt functions
VOID ConInEnable(VOID)
{
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode;

    GetConsoleMode(hInput, &dwMode);
    dwMode |= ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hInput, dwMode);
}

// Used in choice.c!CommandChoice()
VOID ConInFlush(VOID)
{
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);

    if (IsConsoleHandle(hInput))
        FlushConsoleInputBuffer(hInput);
    else
        FlushFileBuffers(hInput);
}

static BOOL
ReadTTYBytes(
    IN HANDLE hInput,
    OUT PCHAR pBuffer,
    IN DWORD  nNumberOfBytesToRead,
    OUT LPDWORD lpNumberOfBytesRead OPTIONAL,
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL)
{
    DWORD dwTotalRead = 0;

    /* Read the leading byte */
    if (!ReadFile(hInput, pBuffer, nNumberOfBytesToRead, /*lpNumberOfBytesRead*/ &dwTotalRead, lpOverlapped) &&
        GetLastError() != ERROR_IO_PENDING)
    {
        return FALSE;
    }

    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = dwTotalRead;

    // FIXME: Deal with overlapped!

    return TRUE;
}

static BOOL
ReadTTYChar(
    IN HANDLE hInput,
    OUT PWCHAR pWChar,
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL)
{
    DWORD dwRead;
    DWORD dwTotalRead;
    CHAR Buffer[6]; // Real maximum number of bytes for a UTF-8 encoded character

    ZeroMemory(Buffer, sizeof(Buffer));
    dwTotalRead = 0;

    /* Read the leading byte */
    if (!ReadTTYBytes(hInput, Buffer, 1, &dwRead, lpOverlapped))
        return FALSE;
    ++dwTotalRead;

    /* Is it an escape sequence? */
    if (Buffer[0] == '\x1B')
    {
        /* Yes it is, let the caller interpret it instead */
        *pWChar = L'\x1B';
        return FALSE;
    }

#if 0 /* Extensions to the UTF-8 encoding */
    if ((Buffer[0] & 0xFE) == 0xFC) /* Check for 1111110x: 1+5-byte encoded character */
    {
        ReadTTYBytes(hInput, &Buffer[1], 5, &dwRead, lpOverlapped);
        dwTotalRead += 5;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80 ||
            (Buffer[4] & 0xC0) != 0x80 ||
            (Buffer[5] & 0xC0) != 0x80)
        {
            return FALSE;
        }
    }
    else
    if ((Buffer[0] & 0xFC) == 0xF8) /* Check for 111110xx: 1+4-byte encoded character */
    {
        ReadTTYBytes(hInput, &Buffer[1], 4, &dwRead, lpOverlapped);
        dwTotalRead += 4;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80 ||
            (Buffer[4] & 0xC0) != 0x80)
        {
            return FALSE;
        }
    }
    else
#endif
    if ((Buffer[0] & 0xF8) == 0xF0) /* Check for 11110xxx: 1+3-byte encoded character */
    {
        ReadTTYBytes(hInput, &Buffer[1], 3, &dwRead, lpOverlapped);
        dwTotalRead += 3;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80)
        {
            return FALSE;
        }
    }
    else
    if ((Buffer[0] & 0xF0) == 0xE0) /* Check for 1110xxxx: 1+2-byte encoded character */
    {
        ReadTTYBytes(hInput, &Buffer[1], 2, &dwRead, lpOverlapped);
        dwTotalRead += 2;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80)
        {
            return FALSE;
        }
    }
    else
    if ((Buffer[0] & 0xE0) == 0xC0) /* Check for 110xxxxx: 1+1-byte encoded character */
    {
        ReadTTYBytes(hInput, &Buffer[1], 1, &dwRead, lpOverlapped);
        ++dwTotalRead;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80)
            return FALSE;
    }
    /* else, this is a 1-byte character */

    /* Convert to UTF-16 */
    return (MultiByteToWideChar(CP_UTF8, 0, Buffer, dwTotalRead, pWChar, 1) == 1);
}

static BOOL
ReadTTYEscapes(
    IN HANDLE hInput,
    OUT PCHAR pEscapeType,
    OUT PCHAR pFunctionChar,
    OUT PSTR pszParams OPTIONAL,
    IN DWORD dwParamsLength,
    OUT PSTR pszInterm OPTIONAL,
    IN DWORD dwIntermLength,
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL)
{
    DWORD dwRead, dwLength;
    PCHAR p;
    CHAR bChar;

    *pEscapeType = 0;
    *pFunctionChar = 0;
    if (pszParams && dwParamsLength > 0)
        *pszParams = 0;
    if (pszInterm && dwIntermLength > 0)
        *pszInterm = 0;

    /*
     * Possibly an escape character, check the second character.
     * Note that we only try to interpret CSI sequences.
     */
    if (!ReadTTYBytes(hInput, &bChar, 1, NULL, lpOverlapped))
        return FALSE;

    if (bChar == 'O')
    {
        /* Single Shift Select of G3 Character Set (SS3) */
        if (!ReadTTYBytes(hInput, &bChar, 1, NULL, lpOverlapped))
            return FALSE;

        *pEscapeType = 'O';
        *pFunctionChar = bChar;
        return TRUE;
    }
    else
    if (bChar == '[')
    {
        /* Control Sequence Introducer (CSI) */

        /* Read any number of parameters */
        dwLength = dwParamsLength;
        p = pszParams;
        dwRead = 0;

        while ((dwRead < dwLength - 1) &&
               ReadTTYBytes(hInput, &bChar, 1, NULL, lpOverlapped))
        {
            /* Is it a paramater? */
            if (0x30 <= bChar && bChar <= 0x3F)
            {
                ++dwRead;
                if (pszParams && dwParamsLength > 0)
                    *p++ = bChar;
            }
            else
            {
                if (pszParams && dwParamsLength > 0)
                    *p = 0;
                break;
            }
        }

        /* Read any number of intermediate bytes */
        dwLength = dwIntermLength;
        p = pszInterm;
        dwRead = 0;

        do
        {
            /* Is it an intermediate byte? */
            if (0x20 <= bChar && bChar <= 0x2F)
            {
                ++dwRead;
                if (pszInterm && dwIntermLength > 0)
                    *p++ = bChar;
            }
            else
            {
                if (pszInterm && dwIntermLength > 0)
                    *p = 0;
                break;
            }
        } while (ReadTTYBytes(hInput, &bChar, 1, NULL, lpOverlapped));

        /* Check the terminating byte */
        if (0x40 <= bChar && bChar <= 0x7E)
        {
            *pEscapeType = '[';
            *pFunctionChar = bChar;
            return TRUE;
        }
        else
        {
            /* Malformed CSI escape sequence, ignore it */
            *pEscapeType = 0;
            *pFunctionChar = 0;
            if (pszParams && dwParamsLength > 0)
                *pszParams = 0;
            if (pszInterm && dwIntermLength > 0)
                *pszInterm = 0;
            return FALSE;
        }
    }
    else
    {
        /* Unsupported escape sequence */
        return FALSE;
    }
}

// Used in choice.c, cmdinput.c, misc.c
DWORD ConInKeyTimeout(PKEY_EVENT_RECORD KeyEvent, DWORD dwMilliseconds)
{
    DWORD dwWaitState;
    HANDLE hInput = ConStreamGetOSHandle(StdIn);
    DWORD dwRead;

    if (hInput == INVALID_HANDLE_VALUE)
    {
        WARN("Invalid input handle!!!\n");
        return FALSE; // No need to make infinite loops!
    }

    if (IsConsoleHandle(hInput))
    {
        INPUT_RECORD ir;

        dwWaitState = WaitForSingleObject(hInput, dwMilliseconds);
        if (dwWaitState == WAIT_TIMEOUT)
            return dwWaitState;
        if (dwWaitState != WAIT_OBJECT_0)
            return dwWaitState; // An error happened.

        /* Be sure there is someting in the console input queue */
        if (!PeekConsoleInput(hInput, &ir, 1, &dwRead))
        {
            /* An error happened, bail out */
            WARN("PeekConsoleInput failed\n");
            return FALSE;
        }

        if (dwRead == 0)
            return FALSE; // TRUE;

        do
        {
            if (!ReadConsoleInput(hInput, &ir, 1, &dwRead))
                return FALSE;
        }
        while ((ir.EventType != KEY_EVENT) || !ir.Event.KeyEvent.bKeyDown);

        /* Got our key, return to caller */
        *KeyEvent = ir.Event.KeyEvent;
    }
    else if (IsTTYHandle(hInput))
    {
        WCHAR wChar;
        WORD  VkKey; // MAKEWORD(low = vkey_code, high = shift_state);
        KEY_EVENT_RECORD KeyEvt;

        if (ReadTTYChar(hInput, &wChar, NULL))
        {
            /* Get the key code (+ shift state) corresponding to the character */
            if (wChar == _T('\0') || wChar >= 0x20 || wChar == _T('\t') /** HACK **/ ||
                wChar == _T('\n') || wChar == _T('\r'))
            {
#ifdef _UNICODE
                VkKey = VkKeyScanW(wChar);
#else
                VkKey = VkKeyScanA(wChar);
#endif
                if (VkKey == 0xFFFF)
                {
                    WARN("FIXME: TODO: VkKeyScanW failed - Should simulate the key!\n");
                    /*
                     * We don't really need the scan/key code because we actually only
                     * use the UnicodeChar for output purposes. It may pose few problems
                     * later on but it's not of big importance. One trick would be to
                     * convert the character to OEM / multibyte and use MapVirtualKey
                     * on each byte (simulating an Alt-0xxx OEM keyboard press).
                     */
                }
            }
            else
            {
                wChar += 0x40;
                VkKey = wChar;
                VkKey |= 0x0200;
            }

#ifdef _UNICODE
            KeyEvt.uChar.UnicodeChar = wChar;
#else
            KeyEvt.uChar.AsciiChar = wChar;
#endif
        }
        else
        {
            CHAR EscapeType, FunctionChar;
            CHAR szParams[255];
            CHAR szInterm[255];

            /* We may have failed because of an escape sequence: check this */
            if (wChar != _T('\x1B'))
            {
                /* Not an escape sequence, bail out */
                return FALSE;
            }

            /*
             * Possibly an escape character, check the second character.
             * Note that we only try to interpret CSI sequences.
             */
            if (!ReadTTYEscapes(hInput, &EscapeType, &FunctionChar,
                                szParams, sizeof(szParams),
                                szInterm, sizeof(szInterm),
                                NULL))
            {
                return FALSE;
            }

            VkKey = 0;

            if (EscapeType == 'O')
            {
                /* Single Shift Select of G3 Character Set (SS3) */

                switch (FunctionChar)
                {
                    case 'A': // Cursor up
                        VkKey = VK_UP;
                        break;

                    case 'B': // Cursor down
                        VkKey = VK_DOWN;
                        break;

                    case 'C': // Cursor right
                        VkKey = VK_RIGHT;
                        break;

                    case 'D': // Cursor left
                        VkKey = VK_LEFT;
                        break;

                    case 'F': // End
                        VkKey = VK_END;
                        break;

                    case 'H': // Home
                        VkKey = VK_HOME;
                        break;

                    case 'P': // F1
                        VkKey = VK_F1;
                        break;

                    case 'Q': // F2
                        VkKey = VK_F2;
                        break;

                    case 'R': // F3
                        VkKey = VK_F3;
                        break;

                    case 'S': // F4
                        VkKey = VK_F4;
                        break;

                    default: // Unknown
                        return FALSE;
                }
            }
            else
            if (EscapeType == '[')
            {
                /* Control Sequence Introducer (CSI) */

                switch (FunctionChar)
                {
                    case 'A': // Cursor up
                        VkKey = VK_UP;
                        break;

                    case 'B': // Cursor down
                        VkKey = VK_DOWN;
                        break;

                    case 'C': // Cursor right
                        VkKey = VK_RIGHT;
                        break;

                    case 'D': // Cursor left
                        VkKey = VK_LEFT;
                        break;

                    case '~': // Some Navigation or Function key
                    {
                        UINT uFnKey = atoi(szParams);

                        switch (uFnKey)
                        {
                            case 1: // Home
                                VkKey = VK_HOME;
                                break;

                            case 2: // Insert
                                VkKey = VK_INSERT;
                                break;

                            case 3: // Delete
                                VkKey = VK_DELETE;
                                break;

                            case 4: // End
                                VkKey = VK_END;
                                break;

                            case 5: // Page UP
                                VkKey = VK_PRIOR;
                                break;

                            case 6: // Page DOWN
                                VkKey = VK_NEXT;
                                break;

                            default:
                            {
                                if (uFnKey < 11)
                                    return FALSE;

                                uFnKey -= 11;
                                if (uFnKey >= 6)
                                    uFnKey--;
                                if (uFnKey >= 10)
                                    uFnKey--;

                                VkKey = VK_F1 + uFnKey;
                            }
                        }

                        break;
                    }
                }
            }
            else
            {
                /* Unsupported escape sequence */
                return FALSE;
            }

            KeyEvt.uChar.UnicodeChar = 0;
        }

        KeyEvt.bKeyDown = TRUE;
        KeyEvt.wRepeatCount = 1;
        KeyEvt.wVirtualKeyCode = LOBYTE(VkKey);
        KeyEvt.wVirtualScanCode = MapVirtualKeyW(LOBYTE(VkKey), MAPVK_VK_TO_VSC);
        KeyEvt.dwControlKeyState = 0;
        if (HIBYTE(VkKey) & 1)
            KeyEvt.dwControlKeyState |= SHIFT_PRESSED;
        if (HIBYTE(VkKey) & 2)
            KeyEvt.dwControlKeyState |= LEFT_CTRL_PRESSED; // RIGHT_CTRL_PRESSED;
        if (HIBYTE(VkKey) & 4)
            KeyEvt.dwControlKeyState |= LEFT_ALT_PRESSED; // RIGHT_ALT_PRESSED;

        /* Got our key, return to caller */
        *KeyEvent = KeyEvt;
    }
    else
    {
        ConOutPuts(L"Not a console input handle!!!\n");
        return FALSE;
    }

    return TRUE;
}

// Used in choice.c, cmdinput.c, misc.c
VOID ConInKey(PKEY_EVENT_RECORD KeyEvent)
{
    ConInKeyTimeout(KeyEvent, INFINITE);
}

// Used in many places...
VOID ConInString(LPTSTR lpInput, DWORD dwLength)
{
    DWORD dwOldMode;
    DWORD dwRead = 0;
    HANDLE hInput;

    LPTSTR p;
    PCHAR pBuf;

#ifdef _UNICODE
    pBuf = (PCHAR)cmd_alloc(dwLength - 1);
#else
    pBuf = lpInput;
#endif
    ZeroMemory(lpInput, dwLength * sizeof(TCHAR));
    hInput = GetStdHandle(STD_INPUT_HANDLE); // ConStreamGetOSHandle(StdIn);
    GetConsoleMode(hInput, &dwOldMode);

    SetConsoleMode(hInput, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

    if (IsConsoleHandle(hInput))
    {
        // FIXME: Note that calling this API will work directly for TTYs or files,
        // but for consoles it will call ReadConsoleA unconditionally so that
        // we will be forced to do some conversion...
        ReadFile(hInput, (PVOID)pBuf, dwLength - 1, &dwRead, NULL);
    }
    else if (IsTTYHandle(hInput))
    {
        p = (PTCHAR)pBuf;
        dwRead = 0;
        while ((dwRead < dwLength - 1) && ReadTTYChar(hInput, p, NULL))
        {
            ++dwRead;

            /* Echo the input character */
            // FIXME: do it if user asked to do so!!
            ConOutChar(*p);

            /* Break if there is a newline */
            if (*p == _T('\r') || *p == _T('\n'))
            {
                *p = _T('\0');
                break;
            }
            ++p;
        }
    }
    else
    {
        /* Directly read the file */
        ReadFile(hInput, (PVOID)pBuf, dwLength - 1, &dwRead, NULL);
    }

#ifdef _UNICODE
    MultiByteToWideChar(InputCodePage, 0, pBuf, dwRead, lpInput, dwLength - 1);
    cmd_free(pBuf);
#endif
    for (p = lpInput; *p; p++)
    {
        if (*p == _T('\r')) // || (*p == _T('\n'))
        {
            *p = _T('\0');
            break;
        }
    }

    SetConsoleMode(hInput, dwOldMode);
}



/******************** Console STREAM OUT utility functions ********************/

VOID __cdecl ConFormatMessage(PCON_STREAM Stream, DWORD MessageId, ...)
{
    INT Len;
    va_list arg_ptr;

    va_start(arg_ptr, MessageId);
    Len = ConMsgPrintfV(Stream,
                        FORMAT_MESSAGE_FROM_SYSTEM,
                        NULL,
                        MessageId,
                        LANG_USER_DEFAULT,
                        &arg_ptr);
    va_end(arg_ptr);

    if (Len <= 0)
        ConResPrintf(Stream, STRING_CONSOLE_ERROR, MessageId);
}



/************************** Console PAGER functions ***************************/

BOOL ConPrintfVPaging(PCON_PAGER Pager, BOOL StartPaging, LPTSTR szFormat, va_list arg_ptr)
{
    // INT len;
    TCHAR szOut[OUTPUT_BUFFER_SIZE];

    /* Return if no string has been given */
    if (szFormat == NULL)
        return TRUE;

    /*len =*/ _vstprintf(szOut, szFormat, arg_ptr);

    // return ConPutsPaging(Pager, PagePrompt, StartPaging, szOut);
    return ConWritePaging(Pager, PagePrompt, StartPaging,
                          szOut, wcslen(szOut));
}

BOOL __cdecl ConOutPrintfPaging(BOOL StartPaging, LPTSTR szFormat, ...)
{
    BOOL bRet;
    va_list arg_ptr;

    va_start(arg_ptr, szFormat);
    bRet = ConPrintfVPaging(&StdOutPager, StartPaging, szFormat, arg_ptr);
    va_end(arg_ptr);
    return bRet;
}

VOID ConOutResPaging(BOOL StartPaging, UINT resID)
{
    ConResPaging(&StdOutPager, PagePrompt, StartPaging, resID);
}



/************************** Console SCREEN functions **************************/

//
// FIXME: FOR TESTING PURPOSES ONLY! WIP
//
#if 0
BOOL
IsPipeHandle(IN HANDLE hHandle)
{
    return ((GetFileType(hHandle) & ~FILE_TYPE_REMOTE) == FILE_TYPE_PIPE);
}

BOOL
IsDiskFileHandle(IN HANDLE hHandle)
{
    return ((GetFileType(hHandle) & ~FILE_TYPE_REMOTE) == FILE_TYPE_DISK);
}

#if 0
    if (IsConsoleHandle(hOutput))
        MessageBoxW(NULL, L"This is a console!", L"Test", MB_OK); // Console
    else if (IsTTYHandle(hOutput))
        MessageBoxW(NULL, L"This is a TTY!", L"Test", MB_OK); // COM port, ...
    else if (IsPipeHandle(hOutput))
        MessageBoxW(NULL, L"This is a pipe!", L"Test", MB_OK);
    else if (IsDiskFileHandle(hOutput))
        MessageBoxW(NULL, L"This is a disk file!", L"Test", MB_OK);
    else
        MessageBoxW(NULL, L"This is an unknown handle!", L"Test", MB_OK);
#endif

#endif



//
// NOTE: Candidate for conutils.c
//
VOID SetCursorXY(SHORT x, SHORT y)
{
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);

    if (IsConsoleHandle(hOutput))
    {
        COORD coPos;
        coPos.X = x;
        coPos.Y = y;
        SetConsoleCursorPosition(hOutput, coPos);
    }
    else if (IsTTYHandle(hOutput))
    {
        ConOutPrintf(_T("\x1B[%d;%dH"), 1 + y, 1 + x);
    }
}

VOID GetCursorXY(PSHORT x, PSHORT y)
{
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);

    if (IsConsoleHandle(hOutput))
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;

        GetConsoleScreenBufferInfo(hOutput, &csbi);
        *x = csbi.dwCursorPosition.X;
        *y = csbi.dwCursorPosition.Y;
    }
    else if (IsTTYHandle(hOutput))
    {
        BOOL Success;
        HANDLE hOutputRead;
        DWORD dwRead, dwLength;
        PCHAR p;
        CHAR bChar;
        CHAR Buffer[20];

        /* Duplicate a handle to StdOut for reading access */
        Success = DuplicateHandle(GetCurrentProcess(),
                                  hOutput,
                                  GetCurrentProcess(),
                                  &hOutputRead,
                                  GENERIC_READ,
                                  FALSE,
                                  0);
        if (Success)
        {
            ConOutPuts(_T("\x1B[6n"));

            /* Read any number of parameters */
            dwLength = sizeof(Buffer);
            p = Buffer;
            dwRead = 0;

            while ((dwRead < dwLength - 1) &&
                   ReadTTYBytes(hOutputRead, &bChar, 1, NULL, NULL))
            {
                if (bChar == 'R')
                {
                    *p++ = bChar;
                    *p = 0;
                    break;
                }

                *p++ = bChar;
                ++dwRead;
            }

            // // ConInString(Buffer, ARRAYSIZE(Buffer));
            // dwRead = ARRAYSIZE(Buffer);
            // ReadFile(hOutputRead, (PVOID)Buffer, dwRead, &dwRead, NULL);

            CloseHandle(hOutputRead);

            sscanf(Buffer, "\x1B[%hu;%huR", y, x);
            --*x; --*y;
        }
    }
}

SHORT GetCursorX(VOID)
{
    SHORT x, y;
    GetCursorXY(&x, &y);
    return x;
}

SHORT GetCursorY(VOID)
{
    SHORT x, y;
    GetCursorXY(&x, &y);
    return y;
}

//
// NOTE: Candidate for conutils.c
//
VOID GetScreenSize(PSHORT maxx, PSHORT maxy)
{
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (IsConsoleHandle(hOutput))
    {
        if (!GetConsoleScreenBufferInfo(hOutput, &csbi))
        {
            csbi.dwSize.X = 80;
            csbi.dwSize.Y = 25;
        }
    }
    else if (IsTTYHandle(hOutput))
    {
        BOOL Success;
        HANDLE hOutputRead;
        DWORD dwRead, dwLength;
        PCHAR p;
        CHAR bChar;
        CHAR Buffer[20];

        SHORT x, y;

        /* Duplicate a handle to StdOut for reading access */
        Success = DuplicateHandle(GetCurrentProcess(),
                                  hOutput,
                                  GetCurrentProcess(),
                                  &hOutputRead,
                                  GENERIC_READ,
                                  FALSE,
                                  0);
        if (Success)
        {
            ConOutPuts(_T("\x1B[18t"));

            /* Read any number of parameters */
            dwLength = sizeof(Buffer);
            p = Buffer;
            dwRead = 0;

            while ((dwRead < dwLength - 1) &&
                   ReadTTYBytes(hOutputRead, &bChar, 1, NULL, NULL))
            {
                if (bChar == 't')
                {
                    *p++ = bChar;
                    *p = 0;
                    break;
                }

                *p++ = bChar;
                ++dwRead;
            }

            // // ConInString(Buffer, ARRAYSIZE(Buffer));
            // dwRead = ARRAYSIZE(Buffer);
            // ReadFile(hOutputRead, (PVOID)Buffer, dwRead, &dwRead, NULL);

            CloseHandle(hOutputRead);

            sscanf(Buffer, "\x1B[8;%hu;%hut", &y, &x);

            csbi.dwSize.X = x;
            csbi.dwSize.Y = y;
        }
    }
    else
    {
        csbi.dwSize.X = 80;
        csbi.dwSize.Y = 25;
    }

    if (maxx) *maxx = csbi.dwSize.X;
    if (maxy) *maxy = csbi.dwSize.Y;
}

//
// NOTE: Candidate for conutils.c
//
VOID SetCursorType(BOOL bInsert, BOOL bVisible)
{
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);

    if (IsConsoleHandle(hOutput))
    {
        CONSOLE_CURSOR_INFO cci;

        cci.dwSize   = bInsert ? 10 : 99;
        cci.bVisible = bVisible;

        SetConsoleCursorInfo(hOutput, &cci);
    }
    else if (IsTTYHandle(hOutput))
    {
        ConOutPrintf(_T("\x1B[%hu q")  // Mode style
                     _T("\x1B[?25%c"), // Visible (h) or hidden (l)
                     bInsert  ?  3  :  1, // Blinking underline (3) or blinking block (1)
                     bVisible ? 'h' : 'l');
    }
}


#ifdef INCLUDE_CMD_COLOR

BOOL ConGetDefaultAttributes(PWORD pwDefAttr)
{
    BOOL Success;
    HANDLE hConsole;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    /* Do not modify *pwDefAttr if we fail, in which case use default attributes */

    hConsole = CreateFile(_T("CONOUT$"), GENERIC_READ|GENERIC_WRITE,
                          FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, 0, NULL);
    if (hConsole == INVALID_HANDLE_VALUE)
        return FALSE; // No default console

    Success = GetConsoleScreenBufferInfo(hConsole, &csbi);
    if (Success)
        *pwDefAttr = csbi.wAttributes;

    CloseHandle(hConsole);
    return Success;
}

#endif


BOOL ConSetTitle(IN LPCTSTR lpConsoleTitle)
{
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);

    /*
     * If the output handle is a TTY (but not console) handle,
     * set also the TTY title. Undefined for devices other than consoles.
     */
    if (!IsConsoleHandle(hOutput) && IsTTYHandle(hOutput))
    {
        /*
         * Use standardized XTerm ANSI sequence:
         *   ESC]0;stringBEL -- Set icon name and window title to string;
         *   ESC]1;stringBEL -- Set icon name to string;
         *   ESC]2;stringBEL -- Set window title to string
         * where ESC is the escape character (\033), and BEL is the
         * bell character (\007).
         */
        ConOutPrintf(_T("\x1B]0;%s\x07"), lpConsoleTitle);
    }

    /* Now really set the console title */
    return SetConsoleTitle(lpConsoleTitle);
}

#ifdef INCLUDE_CMD_BEEP
VOID ConRingBell(HANDLE hOutput)
{
    /* Emit an error beep sound */
    if (IsConsoleHandle(hOutput))
        Beep(800, 200);
    else if (IsTTYHandle(hOutput))
        ConOutPuts(_T("\a")); // BEL character 0x07
    else
        MessageBeep(-1);
}
#endif

/* EOF */
