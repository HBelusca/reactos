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
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
}

// Used in choice.c, cmdinput.c, misc.c
VOID ConInKey(PKEY_EVENT_RECORD KeyEvent)
{
    HANDLE hInput = ConStreamGetOSHandle(StdIn);
    DWORD dwRead;

    if (hInput == INVALID_HANDLE_VALUE)
    {
        WARN("Invalid input handle!!!\n");
        return; // No need to make infinite loops!
    }

    if (IsConsoleHandle(hInput))
    {
        INPUT_RECORD ir;
        do
        {
            ReadConsoleInput(hInput, &ir, 1, &dwRead);
        }
        while ((ir.EventType != KEY_EVENT) || (!ir.Event.KeyEvent.bKeyDown));

        /* Got our key, return to caller */
        *KeyEvent = ir.Event.KeyEvent;
    }
    else if (IsTTYHandle(hInput))
    {
        USHORT VkKey; // MAKEWORD(low = vkey_code, high = shift_state);
        KEY_EVENT_RECORD KeyEvt;
        WCHAR ch;
        CHAR Buffer[6]; // Real maximum number of bytes for a UTF-8 encoded character

        memset(Buffer, 'X', sizeof(Buffer));
        // if (!ReadFile(hInput, &Buffer, ARRAYSIZE(Buffer), &dwRead, NULL))
        if (!ReadFile(hInput, &Buffer, 1, &dwRead, NULL))
            return;

        if (dwRead > 1)
        {
            ConOutPrintf(L"Read more than one char! (dwRead = %d)\n", dwRead);
        }

        ch = *(PCHAR)Buffer; // *(PWCHAR)Buffer;
        // MultiByteToWideChar(...);

        /* Get the key code (+ shift state) corresponding to the character */
        if (ch == '\0' || ch >= 0x20 || ch == '\t' /** HACK **/ || ch == '\n' || ch == '\r')
        {
// #ifdef _UNICODE
            // VkKey = VkKeyScanW(ch);
// #else
            VkKey = VkKeyScanA(ch);
// #endif
            if (VkKey == 0xFFFF)
            {
                ConOutPuts(L"FIXME: TODO: VkKeyScanW failed - Should simulate the key!\n");
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
            ch += 0x40;
            VkKey = ch;
            VkKey |= 0x0200;
        }

        KeyEvt.bKeyDown = TRUE;
        KeyEvt.wRepeatCount = 1;
#ifdef _UNICODE
        KeyEvt.uChar.UnicodeChar = ch;
#else
        KeyEvt.uChar.AsciiChar = ch;
#endif
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
        return;
    }
}

// Used in many places...
VOID ConInString(LPTSTR lpInput, DWORD dwLength)
{
    DWORD dwOldMode;
    DWORD dwRead = 0;
    HANDLE hFile;

    LPTSTR p;
    PCHAR pBuf;

#ifdef _UNICODE
    pBuf = (PCHAR)cmd_alloc(dwLength - 1);
#else
    pBuf = lpInput;
#endif
    ZeroMemory(lpInput, dwLength * sizeof(TCHAR));
    hFile = GetStdHandle(STD_INPUT_HANDLE); // ConStreamGetOSHandle(StdIn);
    GetConsoleMode(hFile, &dwOldMode);

    SetConsoleMode(hFile, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

    // FIXME: Note that calling this API will work directly for TTYs or files,
    // but for consoles it will call ReadConsoleA unconditionally so that
    // we will be forced to do some conversion...
    ReadFile(hFile, (PVOID)pBuf, dwLength - 1, &dwRead, NULL);

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

    SetConsoleMode(hFile, dwOldMode);
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

// FIXME: Not TTY-ready!
VOID GetCursorXY(PSHORT x, PSHORT y)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(ConStreamGetOSHandle(StdOut), &csbi);

    *x = csbi.dwCursorPosition.X;
    *y = csbi.dwCursorPosition.Y;
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
// FIXME: Partially TTY-ready!
VOID GetScreenSize(PSHORT maxx, PSHORT maxy)
{
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);

    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (!GetConsoleScreenBufferInfo(hOutput, &csbi))
    {
        csbi.dwSize.X = 80;
        csbi.dwSize.Y = 25;

#if 0
        if (!IsConsoleHandle(hOutput) && IsTTYHandle(hOutput))
        {
            BOOL Success;
            HANDLE hOutputRead;
            DWORD dwRead;
            TCHAR Buffer[20];

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
                // ConInString(Buffer, ARRAYSIZE(Buffer));
                dwRead = ARRAYSIZE(Buffer);
                ReadFile(hOutputRead, (PVOID)Buffer, dwRead, &dwRead, NULL);
                *Buffer = *Buffer;
                CloseHandle(hOutputRead);
            }
        }
#endif
    }

    if (maxx) *maxx = csbi.dwSize.X;
    if (maxy) *maxy = csbi.dwSize.Y;
}

//
// NOTE: Candidate for conutils.c
//
// FIXME: Not TTY-ready!
VOID SetCursorType(BOOL bInsert, BOOL bVisible)
{
    CONSOLE_CURSOR_INFO cci;

    cci.dwSize = bInsert ? 10 : 99;
    cci.bVisible = bVisible;

    SetConsoleCursorInfo(ConStreamGetOSHandle(StdOut), &cci);
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

#ifdef INCLUDE_CMD_COLOR
BOOL ConSetScreenColor(HANDLE hOutput, WORD wColor, BOOL bFill)
{
    DWORD dwWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD coPos;

    /* Foreground and Background colors can't be the same */
    if ((wColor & 0x0F) == (wColor & 0xF0) >> 4)
        return FALSE;

    /* Fill the whole background if needed */
    if (bFill)
    {
        GetConsoleScreenBufferInfo(hOutput, &csbi);

        coPos.X = 0;
        coPos.Y = 0;
        FillConsoleOutputAttribute(hOutput,
                                   wColor & 0x00FF,
                                   csbi.dwSize.X * csbi.dwSize.Y,
                                   coPos,
                                   &dwWritten);
    }

    /* Set the text attribute */
    SetConsoleTextAttribute(hOutput, wColor & 0x00FF);
    return TRUE;
}
#endif

/* EOF */
