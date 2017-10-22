/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Provides basic abstraction wrappers around CRT streams or
 *              Win32 console API I/O functions, to deal with i18n + Unicode
 *              related problems.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

/**
 * @file    instream.c
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Input
 **/

/*
 * Enable this define if you want to only use CRT functions to output
 * UNICODE stream to the console, as in the way explained by
 * http://archives.miloush.net/michkap/archive/2008/03/18/8306597.html
 */
/** NOTE: Experimental! Don't use USE_CRT yet because output to console is a bit broken **/
// #define USE_CRT

/* FIXME: Temporary HACK before we cleanly support UNICODE functions */
#define UNICODE
#define _UNICODE

#include <tchar.h> // For _T()

#ifdef USE_CRT
#include <fcntl.h>
#include <io.h>
#endif /* USE_CRT */

#include <stdlib.h> // limits.h // For MB_LEN_MAX

#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winuser.h> // MAKEINTRESOURCEW, RT_STRING
#include <wincon.h>  // Console APIs (only if kernel32 support included)
#include <strsafe.h>

/* PSEH for SEH Support */
#include <pseh/pseh2.h>

// HACK!
#define NDEBUG
#include <debug.h>

#include "conutils.h"
#include "stream.h"
#include "stream_private.h"


/* static */ DWORD
ReadBytesAsync(
    IN HANDLE hInput,
    OUT LPVOID pBuffer,
    IN DWORD  nNumberOfBytesToRead,
    OUT LPDWORD lpNumberOfBytesRead OPTIONAL,
    IN DWORD dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL)
{
    DWORD dwTotalRead = 0;

    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = dwTotalRead;

    /* Read the data and write it into the buffer */
    if (!ReadFile(hInput, pBuffer, nNumberOfBytesToRead,
                  /*lpNumberOfBytesRead*/ &dwTotalRead, lpOverlapped))
    {
        DWORD dwLastError = GetLastError();
        if (dwLastError != ERROR_IO_PENDING)
            return dwLastError;

        if (lpOverlapped->hEvent)
        {
            DWORD dwWaitState;

            dwWaitState = WaitForSingleObject(lpOverlapped->hEvent, dwTimeout);
            if (dwWaitState == WAIT_TIMEOUT)
            {
                /*
                 * Properly cancel the I/O operation and wait for the operation
                 * to finish, otherwise the overlapped structure may become
                 * out-of-order while I/O operations are being completed...
                 * See https://blogs.msdn.microsoft.com/oldnewthing/20110202-00/?p=11613
                 * for more details.
                 * NOTE: CancelIoEx does not exist on Windows <= 2003.
                 */
                CancelIo(hInput);
                // CancelIoEx(hInput, &lpOverlapped);
                GetOverlappedResult(hInput, lpOverlapped, &dwTotalRead, TRUE);
                // WaitForSingleObject(lpOverlapped->hEvent, INFINITE);
                return dwWaitState;    // A timeout occurred
            }
            if (dwWaitState != WAIT_OBJECT_0)
                return GetLastError(); // An unknown error happened
        }

        if (!GetOverlappedResult(hInput, lpOverlapped, &dwTotalRead, !lpOverlapped->hEvent))
            return GetLastError();
    }

    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = dwTotalRead;

    return ERROR_SUCCESS;
}

// NOTE: Should be called with the stream locked.
INT
__stdcall
ConReadBytesEx(
    IN PCON_STREAM Stream,
    OUT PCHAR szStr,
    IN DWORD len,
    IN DWORD dwTimeout OPTIONAL) // In milliseconds
{
    DWORD dwRead = 0;

    /* If we do not read anything, just return */
    if (!szStr || len == 0)
        return 0;

    // if (IsConsoleHandle(Stream->hHandle))
    if (Stream->IsConsole)
    {
        // FIXME: Note that calling this API will work directly for TTYs or files,
        // but for consoles it will call ReadConsoleA unconditionally so that
        // we will be forced to do some conversion...

        // FIXME 2: Support timeout!
        ReadFile(Stream->hHandle, (PVOID)szStr, len, &dwRead, &Stream->ovl);
    }
    else // if (IsTTYHandle(Stream->hHandle)) or a regular file
    {
        /* Directly read from the file in asynchronous mode, with timeout support */
        ReadBytesAsync(Stream->hHandle, szStr, len, &dwRead, dwTimeout, &Stream->ovl);
    }

    return dwRead;
}

INT
ConStreamReadBytesEx(
    IN PCON_STREAM Stream,
    OUT PCHAR szStr,
    IN DWORD len,
    IN DWORD dwTimeout OPTIONAL) // In milliseconds
{
    INT Len;

    EnterCriticalSection(&(Stream)->Lock);
    Len = ConReadBytesEx(Stream, szStr, len, dwTimeout);
    LeaveCriticalSection(&(Stream)->Lock);

    return Len;
}

INT
ConStreamReadBytes(
    IN PCON_STREAM Stream,
    OUT PCHAR szStr,
    IN DWORD len)
{
    return ConStreamReadBytesEx(Stream, szStr, len, INFINITE);
}


static DWORD
ReadTTYChar(
    IN HANDLE hInput,
    OUT PTCHAR pChar,
    IN DWORD dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL)
{
    DWORD dwLastError;
    DWORD dwRead;
    DWORD dwTotalRead;
    CHAR Buffer[6]; // Real maximum number of bytes for a UTF-8 encoded character

    ZeroMemory(Buffer, sizeof(Buffer));
    dwTotalRead = 0;

    /* Read the leading byte */
    dwLastError = ReadBytesAsync(hInput, Buffer, 1, &dwRead, dwTimeout, lpOverlapped);
    if (dwLastError != ERROR_SUCCESS)
        return dwLastError;
    ++dwTotalRead;

    /* Is it an escape sequence? */
    if (Buffer[0] == '\x1B')
    {
        /* Yes it is, let the caller interpret it instead */
        *pChar = _T('\x1B');
        return ERROR_SUCCESS;
    }

#if 0 /* Extensions to the UTF-8 encoding */
    if ((Buffer[0] & 0xFE) == 0xFC) /* Check for 1111110x: 1+5-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 5, &dwRead, dwTimeout, lpOverlapped);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 5;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80 ||
            (Buffer[4] & 0xC0) != 0x80 ||
            (Buffer[5] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
    if ((Buffer[0] & 0xFC) == 0xF8) /* Check for 111110xx: 1+4-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 4, &dwRead, dwTimeout, lpOverlapped);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 4;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80 ||
            (Buffer[4] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
#endif
    if ((Buffer[0] & 0xF8) == 0xF0) /* Check for 11110xxx: 1+3-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 3, &dwRead, dwTimeout, lpOverlapped);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 3;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
    if ((Buffer[0] & 0xF0) == 0xE0) /* Check for 1110xxxx: 1+2-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 2, &dwRead, dwTimeout, lpOverlapped);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 2;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
    if ((Buffer[0] & 0xE0) == 0xC0) /* Check for 110xxxxx: 1+1-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 1, &dwRead, dwTimeout, lpOverlapped);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        ++dwTotalRead;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80)
            return ERROR_INVALID_DATA;
    }
    /* else, this is a 1-byte character */

#ifdef _UNICODE
    /* Convert to UTF-16 */
    return (MultiByteToWideChar(CP_UTF8, 0, Buffer, dwTotalRead, pChar, 1) == 1
                ? ERROR_SUCCESS : ERROR_INVALID_DATA);
#else
    #error Not implemented yet!
#endif
}

// NOTE: Should be called with the stream locked.
INT
__stdcall
ConReadCharsEx(
    IN PCON_STREAM Stream,
    OUT PTCHAR szStr,
    IN DWORD len,   // dwLength
    IN DWORD dwTimeout OPTIONAL) // In milliseconds
{
    DWORD dwRead = 0;

    // if (IsConsoleHandle(Stream->hHandle))
    if (Stream->IsConsole)
    {
        // FIXME: Note that calling this API will work directly for TTYs or files,
        // but for consoles it will call ReadConsoleA unconditionally so that
        // we will be forced to do some conversion...

        // FIXME 2: Support timeout!
        ReadFile(Stream->hHandle, (PVOID)szStr, len, &dwRead, NULL);
    }
    else if (IsTTYHandle(Stream->hHandle))
    {
        DWORD dwLastError;
        PTCHAR p;
        OVERLAPPED ovl;

        RtlZeroMemory(&ovl, sizeof(ovl));
        ovl.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        p = (PTCHAR)szStr;
        dwRead = 0;
        while (dwRead < len)
        {
            dwLastError = ReadTTYChar(Stream->hHandle, p, dwTimeout, &ovl);
            if (dwLastError != ERROR_SUCCESS)
                break;

            ++dwRead;

            // /* Echo the input character */
            // // FIXME: do it if user asked to do so!!
            // ConOutChar(*p);

            /* Break if there is a newline */
            if (*p == _T('\r') || *p == _T('\n'))
            {
                *p = _T('\0');
                break;
            }
            ++p;
        }

        CloseHandle(ovl.hEvent);
    }
    else
    {
        /* Directly read from the file in asynchronous mode, with timeout support */
        ReadBytesAsync(Stream->hHandle, szStr, len, &dwRead, dwTimeout, &Stream->ovl);
    }

    return dwRead;
}

INT
ConStreamReadCharsEx(
    IN PCON_STREAM Stream,
    OUT PTCHAR szStr,
    IN DWORD len,   // dwLength
    IN DWORD dwTimeout OPTIONAL) // In milliseconds
{
    INT Len;

    EnterCriticalSection(&(Stream)->Lock);
    Len = ConReadCharsEx(Stream, szStr, len, dwTimeout);
    LeaveCriticalSection(&(Stream)->Lock);

    return Len;
}

INT
ConStreamReadChars(
    IN PCON_STREAM Stream,
    OUT PTCHAR szStr,
    IN DWORD len)   // dwLength
{
    return ConStreamReadCharsEx(Stream, szStr, len, INFINITE);
}

static DWORD
ReadTTYEscapes(
    IN HANDLE hInput,
    OUT PCHAR pEscapeType,
    OUT PCHAR pFunctionChar,
    OUT PSTR pszParams OPTIONAL,
    IN DWORD dwParamsLength,
    OUT PSTR pszInterm OPTIONAL,
    IN DWORD dwIntermLength,
    IN DWORD dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL)
{
    DWORD dwLastError;
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
    dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL, dwTimeout, lpOverlapped);
    if (dwLastError != ERROR_SUCCESS)
        return dwLastError;

    if (bChar == 'O')
    {
        /* Single Shift Select of G3 Character Set (SS3) */
        dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL, dwTimeout, lpOverlapped);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;

        *pEscapeType = 'O';
        *pFunctionChar = bChar;
        return ERROR_SUCCESS;
    }
    else
    if (bChar == '[')
    {
        /* Control Sequence Introducer (CSI) */

        /* Read any number of parameters */
        dwLength = dwParamsLength;
        p = pszParams;
        dwRead = 0;

        while (dwRead < dwLength - 1)
        {
            dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL, dwTimeout, lpOverlapped);
            if (dwLastError != ERROR_SUCCESS)
                return dwLastError; // ERROR_INVALID_DATA;

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

            dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL, dwTimeout, lpOverlapped);
            if (dwLastError != ERROR_SUCCESS)
                return dwLastError; // ERROR_INVALID_DATA;
        } while (dwLastError == ERROR_SUCCESS);

        /* Check the terminating byte */
        if (0x40 <= bChar && bChar <= 0x7E)
        {
            *pEscapeType = '[';
            *pFunctionChar = bChar;
            return ERROR_SUCCESS;
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
            return ERROR_INVALID_DATA;
        }
    }
    else
    {
        /* Unsupported escape sequence */
        return ERROR_INVALID_DATA;
    }
}

DWORD
ConGetKeyTimeout(
    IN PCON_STREAM Stream,
    IN OUT PKEY_EVENT_RECORD KeyEvent,
    IN DWORD dwTimeout) // In milliseconds
{
    if (Stream->hHandle == INVALID_HANDLE_VALUE)
    {
        DPRINT("Invalid input handle!\n");
        return ERROR_INVALID_HANDLE;
    }

    // ConInFlush();
    // FlushFileBuffers(Stream->hHandle);

    // if (IsConsoleHandle(Stream->hHandle))
    if (Stream->IsConsole)
    {
        INPUT_RECORD InputRecords[5];
        ULONG NumRecords, i;
        DWORD dwWaitState;

        do
        {
            dwWaitState = WaitForSingleObject(Stream->hHandle, dwTimeout);
            if (dwWaitState == WAIT_TIMEOUT)
                return dwWaitState;    // A timeout occurred
            if (dwWaitState != WAIT_OBJECT_0)
                return GetLastError(); // An unknown error happened

            /* Be sure there is someting in the console input queue */
            if (!PeekConsoleInput(Stream->hHandle, InputRecords, ARRAYSIZE(InputRecords), &NumRecords))
            {
                /* An error happened, bail out */
                DPRINT("PeekConsoleInput failed\n");
                return GetLastError();
            }

            /* No key events have been detected */
            if (NumRecords == 0)
                continue;

            /*
             * Some events have been detected, pop them out from the input queue.
             * In case we do not use the timer, wait indefinitely until an input
             * event appears.
             */
            if (!ReadConsoleInput(Stream->hHandle, InputRecords, ARRAYSIZE(InputRecords), &NumRecords))
            {
                /* An error happened, bail out */
                return GetLastError();
            }

            /* Check the input events for a key press */
            for (i = 0; i < NumRecords; ++i)
            {
                /* Ignore any non-key event */
                if (InputRecords[i].EventType != KEY_EVENT)
                    continue;

                /* Ignore non-key-down events */
                if (!InputRecords[i].Event.KeyEvent.bKeyDown)
                    continue;

                /* Ignore any system key event */ // FIXME: Should we???
                if ((InputRecords[i].Event.KeyEvent.wVirtualKeyCode == VK_CONTROL) ||
                 // (InputRecords[i].Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED  | RIGHT_ALT_PRESSED )) ||
                 // (InputRecords[i].Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ||
                    (InputRecords[i].Event.KeyEvent.wVirtualKeyCode == VK_MENU)) // || VK_SHIFT ?? FIXME TODO!!
                {
                    continue;
                }

                /* This is a non-system key event: got our key, return to caller */
                *KeyEvent = InputRecords[i].Event.KeyEvent;
                return ERROR_SUCCESS;
            }

            /* No key events have been detected */
        } while (dwTimeout == INFINITE);

        return ERROR_NO_DATA;
    }
    else if (IsTTYHandle(Stream->hHandle))
    {
        DWORD dwLastError;
        OVERLAPPED ovl;
        WCHAR wChar;
        WORD  VkKey; // MAKEWORD(low = vkey_code, high = shift_state);
        KEY_EVENT_RECORD KeyEvt;

        RtlZeroMemory(&ovl, sizeof(ovl));
        ovl.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        dwLastError = ReadTTYChar(Stream->hHandle, &wChar, dwTimeout, &ovl);
        if (dwLastError != ERROR_SUCCESS)
        {
            /* We failed, bail out */
            CloseHandle(ovl.hEvent);
            return dwLastError;
        }

        if (wChar != _T('\x1B'))
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
                    DPRINT("FIXME: TODO: VkKeyScanW failed - Should simulate the key!\n");
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
        else // if (wChar == _T('\x1B'))
        {
            /* We deal with an escape sequence */

            CHAR EscapeType, FunctionChar;
            CHAR szParams[128];
            CHAR szInterm[128];

            /*
             * Try to interpret the escape sequence, and check its type.
             * Note that we only try to interpret a subset of CSI and SS3 sequences.
             */
            dwLastError = ReadTTYEscapes(Stream->hHandle, &EscapeType, &FunctionChar,
                                         szParams, sizeof(szParams),
                                         szInterm, sizeof(szInterm),
                                         dwTimeout, &ovl);
            if (dwLastError != ERROR_SUCCESS)
            {
                /* We failed, bail out */
                CloseHandle(ovl.hEvent);
                return dwLastError;
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
                        CloseHandle(ovl.hEvent);
                        return ERROR_INVALID_DATA;
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
                                    return ERROR_INVALID_DATA;

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
                CloseHandle(ovl.hEvent);
                return ERROR_INVALID_DATA;
            }

            KeyEvt.uChar.UnicodeChar = 0;
        }

        CloseHandle(ovl.hEvent);

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
        return ERROR_SUCCESS;
    }
    else
    {
        DPRINT("Not a console input handle!\n");
        return ERROR_INVALID_HANDLE;
    }
}

/* EOF */
