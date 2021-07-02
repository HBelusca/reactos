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
 * @file    outstream.c
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Output
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

#ifdef USE_CRT
#include <fcntl.h>
#include <io.h>
#endif /* USE_CRT */

#include <stdlib.h> // limits.h // For MB_LEN_MAX

#include <windef.h>
#include <winbase.h>
#include <winnls.h> // For WideCharToMultiByte()
#include <wincon.h> // Console APIs (only if kernel32 support included)

#include "conutils.h"
#include "stream.h"
#include "stream_private.h"


/**
 * @name ConWrite
 *     Writes a counted string to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   szStr
 *     Pointer to the counted string to write.
 *
 * @param[in]   len
 *     Length of the string pointed by @p szStr, specified
 *     in number of characters.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @note
 *     This function is used as an internal function.
 *     Use the ConStreamWrite() function instead.
 *
 * @remark
 *     Should be called with the stream locked.
 **/
INT
__stdcall
ConWrite(
    IN PCON_STREAM Stream,
    IN PCTCH szStr,
    IN DWORD len)
{
#ifndef USE_CRT
    DWORD TotalLen = len, dwNumBytes = 0;
    LPCVOID p;

    /* If we do not write anything, just return */
    if (!szStr || len == 0)
        return 0;

    /* Check whether we are writing to a console */
    // if (IsConsoleHandle(Stream->hHandle))
    if (Stream->IsConsole)
    {
        // TODO: Check if (Stream->Mode == WideText or UTF16Text) ??

        /*
         * This code is inspired from _cputws, in particular from the fact that,
         * according to MSDN: https://msdn.microsoft.com/en-us/library/ms687401(v=vs.85).aspx
         * the buffer size must be less than 64 KB.
         *
         * A similar code can be used for implementing _cputs too.
         */

        DWORD cchWrite;
        TotalLen = len, dwNumBytes = 0;

        while (len > 0)
        {
            cchWrite = min(len, 65535 / sizeof(WCHAR));

            // FIXME: Check return value!
            WriteConsole(Stream->hHandle, szStr, cchWrite, &dwNumBytes, NULL);

            szStr += cchWrite;
            len -= cchWrite;
        }

        return (INT)TotalLen; // FIXME: Really return the number of chars written!
    }

    /*
     * We are redirected and writing to a file or pipe instead of the console.
     * Convert the string from TCHARs to the desired output format, if the two differ.
     *
     * Implementation NOTE:
     *   MultiByteToWideChar (resp. WideCharToMultiByte) are equivalent to
     *   OemToCharBuffW (resp. CharToOemBuffW), but these latter functions
     *   uselessly depend on user32.dll, while MultiByteToWideChar and
     *   WideCharToMultiByte only need kernel32.dll.
     */
    if ((Stream->Mode == WideText) || (Stream->Mode == UTF16Text))
    {
#ifndef _UNICODE // UNICODE means that TCHAR == WCHAR == UTF-16
        /* Convert from the current process/thread's code page to UTF-16 */
        PWCHAR buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(WCHAR));
        if (!buffer)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        len = (DWORD)MultiByteToWideChar(CP_THREAD_ACP, // CP_ACP, CP_OEMCP
                                         0, szStr, (INT)len, buffer, (INT)len);
        szStr = (PVOID)buffer;
#else
        /*
         * Do not perform any conversion since we are already in UTF-16,
         * that is the same encoding as the stream.
         */
#endif

        /*
         * Find any newline character in the buffer,
         * write the part BEFORE the newline, then emit
         * a carriage-return + newline sequence and finally
         * write the remaining part of the buffer.
         *
         * This fixes output in files and serial console.
         */
        while (len > 0)
        {
            /* Loop until we find a newline character */
            p = szStr;
            while (len > 0 && *(PCWCH)p != L'\n')
            {
                /* Advance one character */
                p = (LPCVOID)((PCWCH)p + 1);
                --len;
            }

            /* Write everything up to \n */
            dwNumBytes = ((PCWCH)p - (PCWCH)szStr) * sizeof(WCHAR);
            WriteFile(Stream->hHandle, szStr, dwNumBytes, &dwNumBytes, NULL);

            /*
             * If we hit a newline and the previous character is not a carriage-return,
             * emit a carriage-return + newline sequence, otherwise just emit the newline.
             */
            if (len > 0 && *(PCWCH)p == L'\n')
            {
                if (p == (LPCVOID)szStr || (p > (LPCVOID)szStr && *((PCWCH)p - 1) != L'\r'))
                    WriteFile(Stream->hHandle, L"\r\n", 2 * sizeof(WCHAR), &dwNumBytes, NULL);
                else
                    WriteFile(Stream->hHandle, L"\n", sizeof(WCHAR), &dwNumBytes, NULL);

                /* Skip \n */
                p = (LPCVOID)((PCWCH)p + 1);
                --len;
            }
            szStr = p;
        }

#ifndef _UNICODE
        HeapFree(GetProcessHeap(), 0, buffer);
#endif
    }
    else if ((Stream->Mode == UTF8Text) || (Stream->Mode == AnsiText))
    {
        UINT CodePage;
        PCHAR buffer;

        /*
         * Resolve the current code page if it has not been assigned yet
         * (we do this only if the stream is in ANSI mode; in UTF8 mode
         * the code page is always set to CP_UTF8). Otherwise use the
         * current stream's code page.
         */
        if (/*(Stream->Mode == AnsiText) &&*/ (Stream->CodePage == INVALID_CP))
            CodePage = GetConsoleOutputCP(); // CP_ACP, CP_OEMCP
        else
            CodePage = Stream->CodePage;

#ifdef _UNICODE // UNICODE means that TCHAR == WCHAR == UTF-16
        /* Convert from UTF-16 to either UTF-8 or ANSI, using the stream code page */
        // NOTE: MB_LEN_MAX defined either in limits.h or in stdlib.h .
        buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * MB_LEN_MAX);
        if (!buffer)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        len = WideCharToMultiByte(CodePage, 0,
                                  szStr, len, buffer, len * MB_LEN_MAX,
                                  NULL, NULL);
        szStr = (PVOID)buffer;
#else
        /*
         * Convert from the current process/thread's code page to either
         * UTF-8 or ANSI, using the stream code page.
         * We need to perform a double conversion, by going through UTF-16.
         */
        // TODO!
        #error "Need to implement double conversion!"
#endif

        /*
         * Find any newline character in the buffer,
         * write the part BEFORE the newline, then emit
         * a carriage-return + newline sequence and finally
         * write the remaining part of the buffer.
         *
         * This fixes output in files and serial console.
         */
        while (len > 0)
        {
            /* Loop until we find a newline character */
            p = szStr;
            while (len > 0 && *(PCCH)p != '\n')
            {
                /* Advance one character */
                p = (LPCVOID)((PCCH)p + 1);
                --len;
            }

            /* Write everything up to \n */
            dwNumBytes = ((PCCH)p - (PCCH)szStr) * sizeof(CHAR);
            WriteFile(Stream->hHandle, szStr, dwNumBytes, &dwNumBytes, NULL);

            /*
             * If we hit a newline and the previous character is not a carriage-return,
             * emit a carriage-return + newline sequence, otherwise just emit the newline.
             */
            if (len > 0 && *(PCCH)p == '\n')
            {
                if (p == (LPCVOID)szStr || (p > (LPCVOID)szStr && *((PCCH)p - 1) != '\r'))
                    WriteFile(Stream->hHandle, "\r\n", 2, &dwNumBytes, NULL);
                else
                    WriteFile(Stream->hHandle, "\n", 1, &dwNumBytes, NULL);

                /* Skip \n */
                p = (LPCVOID)((PCCH)p + 1);
                --len;
            }
            szStr = p;
        }

#ifdef _UNICODE
        HeapFree(GetProcessHeap(), 0, buffer);
#else
        // TODO!
#endif
    }
    else // if (Stream->Mode == Binary)
    {
        /* Directly output the string */
        WriteFile(Stream->hHandle, szStr, len, &dwNumBytes, NULL);
    }

    // FIXME!
    return (INT)TotalLen;

#else /* defined(USE_CRT) */

    DWORD total = len;
    DWORD written = 0;

    /* If we do not write anything, just return */
    if (!szStr || len == 0)
        return 0;

#if 1
    /*
     * There is no "counted" printf-to-stream or puts-like function, therefore
     * we use this trick to output the counted string to the stream.
     */
    while (1)
    {
        written = fwprintf(Stream->fStream, L"%.*s", total, szStr);
        if (written < total)
        {
            /*
             * Some embedded NULL or special character
             * was encountered, print it apart.
             */
            if (written == 0)
            {
                fputwc(*szStr, Stream->fStream);
                written++;
            }

            szStr += written;
            total -= written;
        }
        else
        {
            break;
        }
    }
    return (INT)len;
#else
    /* ANSI text or Binary output only */
    _setmode(_fileno(Stream->fStream), _O_TEXT); // _O_BINARY
    return fwrite(szStr, sizeof(*szStr), len, Stream->fStream);
#endif

#endif /* defined(USE_CRT) */
}


#define CON_STREAM_WRITE_CALL(Stream, Str, Len) \
    (Stream)->WriteFunc((Stream), (Str), (Len))

/* Lock the stream only in non-USE_CRT mode (otherwise use the CRT stream lock) */
#ifndef USE_CRT

#define CON_STREAM_WRITE2(Stream, Str, Len, RetLen) \
do { \
    EnterCriticalSection(&(Stream)->Lock); \
    (RetLen) = CON_STREAM_WRITE_CALL((Stream), (Str), (Len)); \
    LeaveCriticalSection(&(Stream)->Lock); \
} while(0)

#else

#define CON_STREAM_WRITE2(Stream, Str, Len, RetLen) \
do { \
    (RetLen) = CON_STREAM_WRITE_CALL((Stream), (Str), (Len)); \
} while(0)

#endif


/**
 * @name ConStreamWrite
 *     Writes a counted string to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   szStr
 *     Pointer to the counted string to write.
 *
 * @param[in]   len
 *     Length of the string pointed by @p szStr, specified
 *     in number of characters.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 **/
INT
ConStreamWrite(
    IN PCON_STREAM Stream,
    IN PCTCH szStr,
    IN DWORD len)
{
    INT Len;
    CON_STREAM_WRITE2(Stream, szStr, len, Len);
    return Len;
}


VOID
ConClearLine(IN PCON_STREAM Stream)
{
    HANDLE hOutput = ConStreamGetOSHandle(Stream);

    /*
     * Erase the full line where the cursor is, and move
     * the cursor back to the beginning of the line.
     */

    if (IsConsoleHandle(hOutput))
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        DWORD dwWritten;

        GetConsoleScreenBufferInfo(hOutput, &csbi);

        csbi.dwCursorPosition.X = 0;
        // csbi.dwCursorPosition.Y;

        FillConsoleOutputCharacterW(hOutput, L' ',
                                    csbi.dwSize.X,
                                    csbi.dwCursorPosition,
                                    &dwWritten);
        SetConsoleCursorPosition(hOutput, csbi.dwCursorPosition);
    }
    else if (IsTTYHandle(hOutput))
    {
        ConPuts(Stream, L"\x1B[2K\x1B[1G"); // FIXME: Just use WriteFile
    }
    // else, do nothing for files
}

/* EOF */
