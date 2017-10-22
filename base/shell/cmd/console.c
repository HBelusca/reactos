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

// Used in choice.c, cmdinput.c, misc.c
DWORD ConInKeyTimeout(PKEY_EVENT_RECORD KeyEvent, DWORD dwMilliseconds)
{
    return ConGetKeyTimeout(StdIn, KeyEvent, dwMilliseconds);
}

// Used in choice.c, cmdinput.c, misc.c
VOID ConInKey(PKEY_EVENT_RECORD KeyEvent)
{
    ConInKeyTimeout(KeyEvent, INFINITE);
}

// Used in many places...
VOID ConInString(LPTSTR lpInput, DWORD dwLength)
{
    LPTSTR p;

    ZeroMemory(lpInput, dwLength * sizeof(TCHAR));

    // dwRead = ConReadCharsEx(StdIn, pBuf, dwLength - 1, INFINITE);
    if (!ReadLine(ConStreamGetOSHandle(StdIn),
                  ConStreamGetOSHandle(StdOut),
                  lpInput, dwLength - 1,
                  CompleteFilename, NULL))
    {
        // return FALSE;
        return;
    }

    for (p = lpInput; *p; p++)
    {
        if (*p == _T('\r')) // || (*p == _T('\n'))
        {
            *p = _T('\0');
            break;
        }
    }
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
    if (IsTTYHandle(hOutput))
        MessageBoxW(NULL, L"This is a TTY!", L"Test", MB_OK); // COM port, ...
    if (IsPipeHandle(hOutput))
        MessageBoxW(NULL, L"This is a pipe!", L"Test", MB_OK);
    if (IsDiskFileHandle(hOutput))
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
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (ConGetScreenInfo(&StdOutScreen, &csbi, CON_SCREEN_CURSORPOS))
    {
        *x = csbi.dwCursorPosition.X;
        *y = csbi.dwCursorPosition.Y;
    }
    else
    {
        *x = *y = 0;
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

VOID GetScreenSize(PSHORT maxx, PSHORT maxy)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (!ConGetScreenInfo(&StdOutScreen, &csbi, CON_SCREEN_SBSIZE))
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
