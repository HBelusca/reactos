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
 * @file    stream.c
 * @ingroup ConUtils
 *
 * @brief   Console I/O streams
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

#include <windef.h>
#include <winbase.h>
#include <winnls.h> // For CP_xxx
#include <wincon.h> // Console APIs (only if kernel32 support included)

#define __CON_STREAM_IMPL
#include "conutils.h"
#include "stream.h"
#include "stream_private.h"


/*
 * Standard console streams, initialized by
 * calls to ConStreamInit/ConInitStdStreams.
 */
#if 0 // FIXME!
CON_STREAM StdStreams[3] =
{
    {0}, // StdIn
    {0}, // StdOut
    {0}, // StdErr
};
#else
CON_STREAM csStdIn;
CON_STREAM csStdOut;
CON_STREAM csStdErr;
#endif


/* Stream translation modes */
#ifdef USE_CRT
/* Lookup table to convert CON_STREAM_MODE to CRT mode */
static int ConToCRTMode[] =
{
    _O_BINARY,  // Binary    (untranslated)
    _O_TEXT,    // AnsiText  (translated)
    _O_WTEXT,   // WideText  (UTF16 with BOM; translated)
    _O_U16TEXT, // UTF16Text (UTF16 without BOM; translated)
    _O_U8TEXT,  // UTF8Text  (UTF8  without BOM; translated)
};

/*
 * See http://archives.miloush.net/michkap/archive/2008/03/18/8306597.html
 * and http://archives.miloush.net/michkap/archive/2009/08/14/9869928.html
 * for more details.
 */

// NOTE1: May the translated mode be cached somehow?
// NOTE2: We may also call IsConsoleHandle to directly set the mode to
//        _O_U16TEXT if it's ok??
// NOTE3: _setmode returns the previous mode, or -1 if failure.
#define CON_STREAM_SET_MODE(Stream, Mode, CacheCodePage)    \
do { \
    fflush((Stream)->Writer.fStream); \
    if ((Mode) < ARRAYSIZE(ConToCRTMode))   \
        _setmode(_fileno((Stream)->Writer.fStream), ConToCRTMode[(Mode)]); \
    else \
        _setmode(_fileno((Stream)->Writer.fStream), _O_TEXT); /* Default to ANSI text */ \
} while(0)

BOOL __stdcall
CrtStreamWriter(
    IN PCON_WRITER Writer,
    IN PCVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pWrittenLength OPTIONAL);

#else /* defined(USE_CRT) */

/*
 * We set Stream->CodePage to INVALID_CP (== -1) to signal that the code page
 * is either not assigned (if the mode is Binary, WideText, or UTF16Text), or
 * is not cached (if the mode is AnsiText). In this latter case the code page
 * is resolved inside ConWrite. Finally, if the mode is UTF8Text, the code page
 * cache is always set to CP_UTF8.
 * The code page cache can be reset by an explicit call to CON_STREAM_SET_MODE
 * (i.e. by calling ConStreamSetMode, or by reinitializing the stream with
 * ConStreamInit(Ex)).
 *
 * NOTE: the reserved values are: 0 (CP_ACP), 1 (CP_OEMCP), 2 (CP_MACCP),
 * 3 (CP_THREAD_ACP), 42 (CP_SYMBOL), 65000 (CP_UTF7) and 65001 (CP_UTF8).
 */
#define CON_STREAM_SET_MODE(Stream, Mode, CacheCodePage)    \
do { \
    (Stream)->Mode = (Mode); \
\
    if ((Mode) == AnsiText)  \
        (Stream)->CodePage = CacheCodePage; /* Possibly assigned */          \
    else if ((Mode) == UTF8Text) \
        (Stream)->CodePage = CP_UTF8;       /* Fixed */                      \
    else /* Mode == Binary, WideText, UTF16Text */                           \
        (Stream)->CodePage = INVALID_CP;    /* Not assigned (meaningless) */ \
} while(0)

BOOL __stdcall
Win32StreamWriter(
    IN PCON_WRITER32 Writer,
    IN PCVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG pWrittenLength OPTIONAL);

#endif /* defined(USE_CRT) */


static BOOL
ConStreamInitEx(
    OUT PCON_STREAM Stream,
    IN  PVOID Handle,
    IN  CON_STREAM_MODE Mode,
    IN  UINT CacheCodePage OPTIONAL /*,
    IN  CON_READ_FUNC ReadFunc OPTIONAL,
    IN  CON_WRITE_FUNC WriteFunc OPTIONAL */)
{
    /* Parameters validation */
    if (!Stream || !Handle || (Mode > UTF8Text))
        return FALSE;

#ifdef USE_CRT

    Stream->Writer.fStream = (FILE*)Handle;
    Stream->Writer.__callFunc = CrtStreamWriter;

#else

    if ((HANDLE)Handle == INVALID_HANDLE_VALUE)
        return FALSE;

    /*
     * As the user calls us by giving us an existing handle to attach on,
     * it is not our duty to close it if we are called again. The user
     * is responsible for having opened those handles, and is responsible
     * for closing them!
     */
#if 0
    /* Attempt to close the handle of the old stream */
    if (/* Stream->IsInitialized && */ Stream->Writer.hHandle &&
        Stream->Writer.hHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Stream->Writer.hHandle);
    }
#endif

    /* Initialize the stream critical section if not already done */
    if (!Stream->IsInitialized)
    {
        InitializeCriticalSection/*AndSpinCount*/(&Stream->Lock /* , 4000 */);
        Stream->IsInitialized = TRUE;
    }

    /* Check whether we are using the console, or are redirected to a file or pipe */
    Stream->IsConsole = IsConsoleHandle(Handle);
    Stream->Writer.hHandle = Handle;
    Stream->Writer.__callFunc = Win32StreamWriter;

#endif /* defined(USE_CRT) */

    /* Set the correct file translation mode */
    CON_STREAM_SET_MODE(Stream, Mode, CacheCodePage);

    return TRUE;
}

BOOL
ConStreamInit(
    IN OUT PCON_STREAM Stream,
    IN PVOID Handle,
    // IN ReadWriteMode ????
    IN CON_STREAM_MODE Mode,
    IN UINT CacheCodePage OPTIONAL)
{
    return ConStreamInitEx(Stream, Handle, Mode, CacheCodePage);
}

BOOL
ConStreamSetMode(
    IN PCON_STREAM Stream,
    IN CON_STREAM_MODE Mode,
    IN UINT CacheCodePage OPTIONAL)
{
    /* Parameters validation */
    if (!Stream || (Mode > UTF8Text))
        return FALSE;

#ifdef USE_CRT
    if (!Stream->Writer.fStream)
        return FALSE;
#endif

    /* Set the correct file translation mode */
    CON_STREAM_SET_MODE(Stream, Mode, CacheCodePage);
    return TRUE;
}

BOOL
ConStreamSetCacheCodePage(
    IN PCON_STREAM Stream,
    IN UINT CacheCodePage)
{
#ifdef USE_CRT
// FIXME!
#warning The ConStreamSetCacheCodePage function does not make much sense with the CRT!
#else
    CON_STREAM_MODE Mode;

    /* Parameters validation */
    if (!Stream)
        return FALSE;

    /*
     * Keep the original stream mode but set the correct file code page
     * (will be reset only if Mode == AnsiText).
     */
    Mode = Stream->Mode;
    CON_STREAM_SET_MODE(Stream, Mode, CacheCodePage);
#endif
    return TRUE;
}


HANDLE
ConStreamGetOSHandle(
    IN PCON_STREAM Stream)
{
    /* Parameters validation */
    if (!Stream)
        return INVALID_HANDLE_VALUE;

    /*
     * See https://web.archive.org/web/20100109134746/https://support.microsoft.com/kb/99173
     * for more details.
     */

#ifdef USE_CRT
    if (!Stream->Writer.fStream)
        return INVALID_HANDLE_VALUE;

    return (HANDLE)_get_osfhandle(_fileno(Stream->Writer.fStream));
#else
    return Stream->Writer.hHandle;
#endif
}

BOOL
ConStreamSetOSHandle(
    IN PCON_STREAM Stream,
    IN HANDLE Handle)
{
    /* Parameters validation */
    if (!Stream)
        return FALSE;

    /*
     * See https://web.archive.org/web/20100109134746/https://support.microsoft.com/kb/99173
     * for more details.
     */

#ifdef USE_CRT
    if (!Stream->Writer.fStream)
        return FALSE;

    int fdOut = _open_osfhandle((intptr_t)Handle, _O_TEXT /* FIXME! */);
    FILE* fpOut = _fdopen(fdOut, "w");
    *Stream->Writer.fStream = *fpOut;
    /// setvbuf(Stream->Writer.fStream, NULL, _IONBF, 0);
    // Stream->Writer.__callFunc = CrtStreamWriter;

    return TRUE;
#else
    /* Flush the stream and reset its handle */
    if (Stream->Writer.hHandle != INVALID_HANDLE_VALUE)
        FlushFileBuffers(Stream->Writer.hHandle);

    /* Check whether we are using the console, or are redirected to a file or pipe */
    Stream->IsConsole = IsConsoleHandle(Handle);
    Stream->Writer.hHandle = Handle;
    Stream->Writer.__callFunc = Win32StreamWriter;

    // NOTE: Mode reset??

    return TRUE;
#endif
}

/* EOF */
