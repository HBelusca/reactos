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
 * @file    instream.h
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Input
 **/

#ifndef __INSTREAM_H__
#define __INSTREAM_H__

#pragma once

/*
 * Enable this define if you want to only use CRT functions to output
 * UNICODE stream to the console, as in the way explained by
 * http://archives.miloush.net/michkap/archive/2008/03/18/8306597.html
 */
/** NOTE: Experimental! Don't use USE_CRT yet because output to console is a bit broken **/
// #define USE_CRT

#ifndef _UNICODE
#error The ConUtils library at the moment only supports compilation with _UNICODE defined!
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <wincon.h>

// Shadow type, implementation-specific
typedef struct _CON_STREAM CON_STREAM, *PCON_STREAM;

// typedef INT (__stdcall *CON_READ_FUNC)(IN PCON_STREAM, IN PTCHAR, IN DWORD);
                                        // Stream,         szStr,     len
typedef INT (__stdcall *CON_WRITE_FUNC)(IN PCON_STREAM, IN PTCHAR, IN DWORD);


/* static */ DWORD
ReadBytesAsync(
    IN HANDLE hInput,
    OUT LPVOID pBuffer,
    IN DWORD  nNumberOfBytesToRead,
    OUT LPDWORD lpNumberOfBytesRead OPTIONAL,
    IN DWORD dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL);

INT
__stdcall
ConReadBytesEx(
    IN PCON_STREAM Stream,
    OUT PCHAR szStr,
    IN DWORD len,
    IN DWORD dwTimeout OPTIONAL); // In milliseconds

INT
ConStreamReadBytesEx(
    IN PCON_STREAM Stream,
    OUT PCHAR szStr,
    IN DWORD len,
    IN DWORD dwTimeout OPTIONAL); // In milliseconds

INT
ConStreamReadBytes(
    IN PCON_STREAM Stream,
    OUT PCHAR szStr,
    IN DWORD len);

INT
__stdcall
ConReadCharsEx(
    IN PCON_STREAM Stream,
    OUT PTCHAR szStr,
    IN DWORD len,   // dwLength
    IN DWORD dwTimeout OPTIONAL); // In milliseconds

INT
ConStreamReadCharsEx(
    IN PCON_STREAM Stream,
    OUT PTCHAR szStr,
    IN DWORD len,   // dwLength
    IN DWORD dwTimeout OPTIONAL); // In milliseconds

INT
ConStreamReadChars(
    IN PCON_STREAM Stream,
    OUT PTCHAR szStr,
    IN DWORD len);   // dwLength

DWORD
ConGetKeyTimeout(
    IN PCON_STREAM Stream,
    IN OUT PKEY_EVENT_RECORD KeyEvent,
    IN DWORD dwTimeout); // In milliseconds

#ifdef __cplusplus
}
#endif

#endif  /* __INSTREAM_H__ */

/* EOF */
