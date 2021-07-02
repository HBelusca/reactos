/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Provides basic abstraction wrappers around CRT streams or
 *              Win32 console API I/O functions, to deal with i18n + Unicode
 *              related problems.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

#ifndef __STREAM_PRIVATE_H__
#define __STREAM_PRIVATE_H__

#pragma once

/*
 * Console I/O streams
 */

typedef struct _CON_STREAM
{
#if defined(USE_WIN32_READWRITE)
    WIN32_WRITER32 Writer;
    // WIN32_READER32 Reader;
#elif defined(USE_CRT_READWRITE)
    CRT_WRITER Writer;
    // CRT_READER Reader;
#endif
    // TODO: When instream implemented, add a CON_READER.

#ifdef USE_WIN32_READWRITE // && !defined(USE_CRT)
    CRITICAL_SECTION Lock;

    struct
    {
        BOOL IsInitialized : 1;
        /*
         * TRUE if 'hHandle' refers to a console, in which case I/O UTF-16
         * is directly used. If the Writer/Reader refers to a file or a pipe,
         * the 'Mode' flag is used.
         */
        BOOL IsConsole : 1;
    };

    /*
     * The 'Mode' flag is used to know the translation mode
     * when 'hHandle' refers to a file or a pipe.
     */
    CON_STREAM_MODE Mode;
    UINT CodePage;  // Used to convert UTF-16 text to some ANSI code page.
#endif /* defined(USE_WIN32_READWRITE) */

} CON_STREAM, *PCON_STREAM;

#endif  /* __STREAM_PRIVATE_H__ */

/* EOF */
