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
 * @file    outstream.h
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Output
 **/

#ifndef __OUTSTREAM_H__
#define __OUTSTREAM_H__

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

// Shadow type, implementation-specific
typedef struct _CON_STREAM CON_STREAM, *PCON_STREAM;

// typedef INT (__stdcall *CON_READ_FUNC)(
    // IN PCON_STREAM Stream,
    // OUT PTCHAR szStr,
    // IN OUT PDWORD len);

typedef INT (__stdcall *CON_WRITE_FUNC)(
    IN PCON_STREAM Stream,
    IN PCTCH szStr,
    IN DWORD len);


INT
__stdcall
ConWrite(
    IN PCON_STREAM Stream,
    IN PCTCH szStr,
    IN DWORD len);

INT
ConStreamWrite(
    IN PCON_STREAM Stream,
    IN PCTCH szStr,
    IN DWORD len);


VOID
ConClearLine(IN PCON_STREAM Stream);


#ifdef __cplusplus
}
#endif

#endif  /* __OUTSTREAM_H__ */

/* EOF */
