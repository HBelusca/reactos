/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Print helper functions.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2021 Hermes Belusca-Maito
 */

/**
 * @file    conprint.c
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Print helper functions.
 **/

/* FIXME: Temporary HACK before we cleanly support UNICODE functions */
#define UNICODE
#define _UNICODE

// #include <stdlib.h> // limits.h // For MB_LEN_MAX

#include <windef.h>
#include <winbase.h>
#include <strsafe.h>

/* PSEH for SEH Support */
#include <pseh/pseh2.h>

#define __CON_STREAM_IMPL
#include "conutils.h"

// Also known as: RC_STRING_MAX_SIZE, MAX_BUFFER_SIZE (some programs:
// wlanconf, shutdown, set it to 5024), OUTPUT_BUFFER_SIZE (name given
// in cmd/console.c), MAX_STRING_SIZE (name given in diskpart) or
// MAX_MESSAGE_SIZE (set to 512 in shutdown).
#define CON_RC_STRING_MAX_SIZE  4096


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
DWORD
ConWrite(
    IN PCON_WRITER32 Writer,
    IN PCTCH szStr,
    IN DWORD Len)
{
    Len *= sizeof(TCHAR);
    if (!CALL_W32(Writer)(szStr, Len, &Len))
        Len = 0; /* Fixup returned length in case of errors */
    return (Len / sizeof(TCHAR));
}

/**
 * @name ConPuts
 *     Writes a NULL-terminated string to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   szStr
 *     Pointer to the NULL-terminated string to write.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @remark
 *     Contrary to the CRT puts() function, ConPuts() does not append
 *     a terminating new-line character. In this way it behaves more like
 *     the CRT fputs() function.
 **/
DWORD
ConPuts(
    IN PCON_WRITER32 Writer,
    IN PCWSTR szStr)
{
    DWORD Len;

    Len = (DWORD)wcslen(szStr) * sizeof(WCHAR);
    if (!CALL_W32(Writer)(szStr, Len, &Len))
        Len = 0; /* Fixup returned length in case of errors */
    return (Len / sizeof(WCHAR));
}

/**
 * @name ConPrintfV
 *     Formats and writes a NULL-terminated string to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   szStr
 *     Pointer to the NULL-terminated format string, that follows the same
 *     specifications as the @a szStr format string in ConPrintf().
 *
 * @param[in]   args
 *     Parameter describing a variable number of arguments,
 *     initialized with va_start(), that can be expected by the function,
 *     depending on the @p szStr format string. Each argument is used to
 *     replace a <em>format specifier</em> in the format string.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), printf(), vprintf()
 **/
DWORD
ConPrintfV(
    IN PCON_WRITER32 Writer,
    IN PCWSTR  szStr,
    IN va_list args)
{
    DWORD Len;
    WCHAR bufSrc[CON_RC_STRING_MAX_SIZE];

    // Len = (DWORD)vfwprintf(Writer->fStream, szStr, args); // vfprintf for direct ANSI

    /*
     * Re-use szStr as the pointer to end-of-string, so as
     * to compute the string length instead of calling wcslen().
     */
    StringCchVPrintfExW(bufSrc, ARRAYSIZE(bufSrc), (PWSTR*)&szStr, NULL, 0, szStr, args);
    Len = (szStr - bufSrc) * sizeof(WCHAR);

    if (!CALL_W32(Writer)(bufSrc, Len, &Len))
        Len = 0; /* Fixup returned length in case of errors */
    return (Len / sizeof(WCHAR));
}

/**
 * @name ConPrintf
 *     Formats and writes a NULL-terminated string to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   szStr
 *     Pointer to the NULL-terminated format string, that follows the same
 *     specifications as the @a format string in printf(). This string can
 *     optionally contain embedded <em>format specifiers</em> that are
 *     replaced by the values specified in subsequent additional arguments
 *     and formatted as requested.
 *
 * @param[in]   ...
 *     Additional arguments that can be expected by the function, depending
 *     on the @p szStr format string. Each argument is used to replace a
 *     <em>format specifier</em> in the format string.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintfV(), printf(), vprintf()
 **/
DWORD
__cdecl
ConPrintf(
    IN PCON_WRITER32 Writer,
    IN PCWSTR szStr,
    ...)
{
    DWORD Len;
    va_list args;

    // Len = vfwprintf(Writer->fStream, szMsgBuf, args); // vfprintf for direct ANSI

    va_start(args, szStr);
    Len = ConPrintfV(Writer, szStr, args);
    va_end(args);

    return Len;
}

/**
 * @name ConResPutsEx
 *     Writes a string resource to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   hInstance
 *     Optional handle to an instance of the module whose executable file
 *     contains the string resource. Can be set to NULL to get the handle
 *     to the application itself.
 *
 * @param[in]   uID
 *     The identifier of the string to be written.
 *
 * @param[in]   LanguageId
 *     The language identifier of the resource. If this parameter is
 *     <tt>MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)</tt>, the current language
 *     associated with the calling thread is used. To specify a language other
 *     than the current language, use the @c MAKELANGID macro to create this
 *     parameter.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @remark
 *     Similarly to ConPuts(), no terminating new-line character is appended.
 *
 * @see ConPuts(), ConResPuts()
 **/
DWORD
ConResPutsEx(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT   uID,
    IN LANGID LanguageId)
{
    PWCHAR szStr = NULL;
    DWORD Len;

    Len = (DWORD)K32LoadStringExW(hInstance, uID, LanguageId, (PWSTR)&szStr, 0);
    Len *= sizeof(WCHAR);
    if (!(szStr && Len && CALL_W32(Writer)(szStr, Len, &Len)))
        Len = 0; /* Fixup returned length in case of errors */
    return (Len / sizeof(WCHAR));
}

/**
 * @name ConResPuts
 *     Writes a string resource contained in the current application
 *     to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   uID
 *     The identifier of the string to be written.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @remark
 *     Similarly to ConPuts(), no terminating new-line character is appended.
 *
 * @see ConPuts(), ConResPutsEx()
 **/
DWORD
ConResPuts(
    IN PCON_WRITER32 Writer,
    IN UINT uID)
{
    return ConResPutsEx(Writer, NULL /*GetModuleHandleW(NULL)*/,
                        uID, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
}

/**
 * @name ConResPrintfExV
 *     Formats and writes a string resource to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   hInstance
 *     Optional handle to an instance of the module whose executable file
 *     contains the string resource. Can be set to NULL to get the handle
 *     to the application itself.
 *
 * @param[in]   uID
 *     The identifier of the format string. The format string follows the
 *     same specifications as the @a szStr format string in ConPrintf().
 *
 * @param[in]   LanguageId
 *     The language identifier of the resource. If this parameter is
 *     <tt>MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)</tt>, the current language
 *     associated with the calling thread is used. To specify a language other
 *     than the current language, use the @c MAKELANGID macro to create this
 *     parameter.
 *
 * @param[in]   args
 *     Parameter describing a variable number of arguments,
 *     initialized with va_start(), that can be expected by the function,
 *     depending on the @p szStr format string. Each argument is used to
 *     replace a <em>format specifier</em> in the format string.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintfEx(), ConResPrintfV(), ConResPrintf()
 **/
DWORD
ConResPrintfExV(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT    uID,
    IN LANGID  LanguageId,
    IN va_list args)
{
    DWORD Len;
    WCHAR bufSrc[CON_RC_STRING_MAX_SIZE];

    // NOTE: We may use the special behaviour where nBufMaxSize == 0
    Len = K32LoadStringExW(hInstance, uID, LanguageId, bufSrc, ARRAYSIZE(bufSrc));
    if (Len)
        Len = ConPrintfV(Writer, bufSrc, args);

    return Len;
}

/**
 * @name ConResPrintfV
 *     Formats and writes a string resource contained in the
 *     current application to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   uID
 *     The identifier of the format string. The format string follows the
 *     same specifications as the @a szStr format string in ConPrintf().
 *
 * @param[in]   args
 *     Parameter describing a variable number of arguments,
 *     initialized with va_start(), that can be expected by the function,
 *     depending on the @p szStr format string. Each argument is used to
 *     replace a <em>format specifier</em> in the format string.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintfExV(), ConResPrintfEx(), ConResPrintf()
 **/
DWORD
ConResPrintfV(
    IN PCON_WRITER32 Writer,
    IN UINT    uID,
    IN va_list args)
{
    return ConResPrintfExV(Writer, NULL /*GetModuleHandleW(NULL)*/,
                           uID, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                           args);
}

/**
 * @name ConResPrintfEx
 *     Formats and writes a string resource to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   hInstance
 *     Optional handle to an instance of the module whose executable file
 *     contains the string resource. Can be set to NULL to get the handle
 *     to the application itself.
 *
 * @param[in]   uID
 *     The identifier of the format string. The format string follows the
 *     same specifications as the @a szStr format string in ConPrintf().
 *
 * @param[in]   LanguageId
 *     The language identifier of the resource. If this parameter is
 *     <tt>MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)</tt>, the current language
 *     associated with the calling thread is used. To specify a language other
 *     than the current language, use the @c MAKELANGID macro to create this
 *     parameter.
 *
 * @param[in]   ...
 *     Additional arguments that can be expected by the function, depending
 *     on the @p szStr format string. Each argument is used to replace a
 *     <em>format specifier</em> in the format string.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintfExV(), ConResPrintfV(), ConResPrintf()
 **/
DWORD
__cdecl
ConResPrintfEx(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT   uID,
    IN LANGID LanguageId,
    ...)
{
    DWORD Len;
    va_list args;

    va_start(args, LanguageId);
    Len = ConResPrintfExV(Writer, hInstance, uID, LanguageId, args);
    va_end(args);

    return Len;
}

/**
 * @name ConResPrintf
 *     Formats and writes a string resource contained in the
 *     current application to a stream.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   uID
 *     The identifier of the format string. The format string follows the
 *     same specifications as the @a szStr format string in ConPrintf().
 *
 * @param[in]   ...
 *     Additional arguments that can be expected by the function, depending
 *     on the @p szStr format string. Each argument is used to replace a
 *     <em>format specifier</em> in the format string.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintfExV(), ConResPrintfEx(), ConResPrintfV()
 **/
DWORD
__cdecl
ConResPrintf(
    IN PCON_WRITER32 Writer,
    IN UINT uID,
    ...)
{
    DWORD Len;
    va_list args;

    va_start(args, uID);
    Len = ConResPrintfV(Writer, uID, args);
    va_end(args);

    return Len;
}

/**
 * @name ConMsgPuts
 *     Writes a message string to a stream without formatting. The function
 *     requires a message definition as input. The message definition can come
 *     from a buffer passed to the function. It can come from a message table
 *     resource in an already-loaded module, or the caller can ask the function
 *     to search the system's message table resource(s) for the message definition.
 *     Please refer to the Win32 FormatMessage() function for more details.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   dwFlags
 *     The formatting options, and how to interpret the @p lpSource parameter.
 *     See FormatMessage() for more details. The @b FORMAT_MESSAGE_ALLOCATE_BUFFER
 *     and @b FORMAT_MESSAGE_ARGUMENT_ARRAY flags are always ignored.
 *     The function implicitly uses the @b FORMAT_MESSAGE_IGNORE_INSERTS flag
 *     to implement its behaviour.
 *
 * @param[in]   lpSource
 *     The location of the message definition. The type of this parameter
 *     depends upon the settings in the @p dwFlags parameter.
 *
 * @param[in]   dwMessageId
 *     The message identifier for the requested message. This parameter
 *     is ignored if @p dwFlags includes @b FORMAT_MESSAGE_FROM_STRING.
 *
 * @param[in]   dwLanguageId
 *     The language identifier for the requested message. This parameter
 *     is ignored if @p dwFlags includes @b FORMAT_MESSAGE_FROM_STRING.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @remark
 *     Similarly to ConPuts(), no terminating new-line character is appended.
 *
 * @see ConPuts(), ConResPuts() and associated functions,
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
ConMsgPuts(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId)
{
    PWSTR lpMsgBuf = NULL;
    DWORD Len = 0;

    /*
     * Sanitize dwFlags. This version always ignores explicitly the inserts
     * as we emulate the behaviour of the (f)puts function.
     */
    dwFlags |= FORMAT_MESSAGE_ALLOCATE_BUFFER; // Always allocate an internal buffer.
    dwFlags |= FORMAT_MESSAGE_IGNORE_INSERTS;  // Ignore inserts for FormatMessage.
    dwFlags &= ~FORMAT_MESSAGE_ARGUMENT_ARRAY;

    /*
     * Retrieve the message string without appending extra newlines.
     * Wrap in SEH to protect from invalid string parameters.
     */
    _SEH2_TRY
    {
        Len = FormatMessageW(dwFlags,
                             lpSource,
                             dwMessageId,
                             dwLanguageId,
                             (PWSTR)&lpMsgBuf,
                             0,
                             NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    if (!lpMsgBuf)
    {
        // ASSERT(Len == 0);
    }
    else
    {
        // ASSERT(Len != 0);

        /* lpMsgBuf is NULL-terminated by FormatMessage */
        // Len = ConPuts(Writer, lpMsgBuf);
        Len *= sizeof(WCHAR);
        if (!CALL_W32(Writer)(lpMsgBuf, Len, &Len))
            Len = 0; /* Fixup returned length in case of errors */
        Len /= sizeof(WCHAR);

        /* Free the buffer allocated by FormatMessage */
        LocalFree(lpMsgBuf);
    }

    return Len;
}

/**
 * @name ConMsgPrintf2V
 *     Formats and writes a message string to a stream.
 *
 * @remark For internal use only.
 *
 * @see ConMsgPrintfV()
 **/
DWORD
ConMsgPrintf2V(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    IN va_list args)
{
    PWSTR lpMsgBuf = NULL;
    DWORD Len = 0;

    /*
     * Sanitize dwFlags. This version always ignores explicitly the inserts.
     * The string that we will return to the user will not be pre-formatted.
     */
    dwFlags |= FORMAT_MESSAGE_ALLOCATE_BUFFER; // Always allocate an internal buffer.
    dwFlags |= FORMAT_MESSAGE_IGNORE_INSERTS;  // Ignore inserts for FormatMessage.
    dwFlags &= ~FORMAT_MESSAGE_ARGUMENT_ARRAY;

    /*
     * Retrieve the message string without appending extra newlines.
     * Wrap in SEH to protect from invalid string parameters.
     */
    _SEH2_TRY
    {
        Len = FormatMessageW(dwFlags,
                             lpSource,
                             dwMessageId,
                             dwLanguageId,
                             (PWSTR)&lpMsgBuf,
                             0,
                             NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    if (!lpMsgBuf)
    {
        // ASSERT(Len == 0);
    }
    else
    {
        // ASSERT(Len != 0);

        /* lpMsgBuf is NULL-terminated by FormatMessage */
        Len = ConPrintfV(Writer, lpMsgBuf, args);
        // Len *= sizeof(WCHAR);
        // if (!CALL_W32(Writer)(lpMsgBuf, Len, &Len))
        //     Len = 0; /* Fixup returned length in case of errors */
        // Len /= sizeof(WCHAR);

        /* Free the buffer allocated by FormatMessage */
        LocalFree(lpMsgBuf);
    }

    return Len;
}

/**
 * @name ConMsgPrintfV
 *     Formats and writes a message string to a stream. The function requires
 *     a message definition as input. The message definition can come from a
 *     buffer passed to the function. It can come from a message table resource
 *     in an already-loaded module, or the caller can ask the function to search
 *     the system's message table resource(s) for the message definition.
 *     Please refer to the Win32 FormatMessage() function for more details.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   dwFlags
 *     The formatting options, and how to interpret the @p lpSource parameter.
 *     See FormatMessage() for more details.
 *     The @b FORMAT_MESSAGE_ALLOCATE_BUFFER flag is always ignored.
 *
 * @param[in]   lpSource
 *     The location of the message definition. The type of this parameter
 *     depends upon the settings in the @p dwFlags parameter.
 *
 * @param[in]   dwMessageId
 *     The message identifier for the requested message. This parameter
 *     is ignored if @p dwFlags includes @b FORMAT_MESSAGE_FROM_STRING.
 *
 * @param[in]   dwLanguageId
 *     The language identifier for the requested message. This parameter
 *     is ignored if @p dwFlags includes @b FORMAT_MESSAGE_FROM_STRING.
 *
 * @param[in]   Arguments
 *     Optional pointer to an array of values describing a variable number of
 *     arguments, depending on the message string. Each argument is used to
 *     replace an <em>insert sequence</em> in the message string.
 *     By default, the @p Arguments parameter is of type @c va_list*, initialized
 *     with va_start(). The state of the @c va_list argument is undefined upon
 *     return from the function. To use the @c va_list again, destroy the variable
 *     argument list pointer using va_end() and reinitialize it with va_start().
 *     If you do not have a pointer of type @c va_list*, then specify the
 *     @b FORMAT_MESSAGE_ARGUMENT_ARRAY flag and pass a pointer to an array
 *     of @c DWORD_PTR values; those values are input to the message formatted
 *     as the insert values. Each insert must have a corresponding element in
 *     the array.
 *
 * @remark
 *     Contrary to printf(), ConPrintf(), ConResPrintf() and associated functions,
 *     the ConMsg* functions work on format strings that contain <em>insert sequences</em>.
 *     These sequences extend the standard <em>format specifiers</em> as they
 *     allow to specify an <em>insert number</em> referring which precise value
 *     given in arguments to use.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintf() and associated functions, ConMsgPrintf(),
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
ConMsgPrintfV(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    IN va_list *Arguments OPTIONAL)
{
    PWSTR lpMsgBuf = NULL;
    DWORD Len;

    /* Sanitize dwFlags */
    dwFlags |= FORMAT_MESSAGE_ALLOCATE_BUFFER; // Always allocate an internal buffer.

    /*
     * Retrieve the message string without appending extra newlines.
     * Use the "safe" FormatMessage version (SEH-protected) to protect
     * from invalid string parameters.
     */
    Len = FormatMessageSafeW(dwFlags,
                             lpSource,
                             dwMessageId,
                             dwLanguageId,
                             (PWSTR)&lpMsgBuf,
                             0,
                             Arguments);

    if (!lpMsgBuf)
    {
        // ASSERT(Len == 0);
    }
    else
    {
        // ASSERT(Len != 0);

        Len *= sizeof(WCHAR);
        if (!CALL_W32(Writer)(lpMsgBuf, Len, &Len))
            Len = 0; /* Fixup returned length in case of errors */
        Len /= sizeof(WCHAR);

        /* Free the buffer allocated by FormatMessage */
        LocalFree(lpMsgBuf);
    }

    return Len;
}

/**
 * @name ConMsgPrintf
 *     Formats and writes a message string to a stream. The function requires
 *     a message definition as input. The message definition can come from a
 *     buffer passed to the function. It can come from a message table resource
 *     in an already-loaded module, or the caller can ask the function to search
 *     the system's message table resource(s) for the message definition.
 *     Please refer to the Win32 FormatMessage() function for more details.
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   dwFlags
 *     The formatting options, and how to interpret the @p lpSource parameter.
 *     See FormatMessage() for more details. The @b FORMAT_MESSAGE_ALLOCATE_BUFFER
 *     and @b FORMAT_MESSAGE_ARGUMENT_ARRAY flags are always ignored.
 *
 * @param[in]   lpSource
 *     The location of the message definition. The type of this parameter
 *     depends upon the settings in the @p dwFlags parameter.
 *
 * @param[in]   dwMessageId
 *     The message identifier for the requested message. This parameter
 *     is ignored if @p dwFlags includes @b FORMAT_MESSAGE_FROM_STRING.
 *
 * @param[in]   dwLanguageId
 *     The language identifier for the requested message. This parameter
 *     is ignored if @p dwFlags includes @b FORMAT_MESSAGE_FROM_STRING.
 *
 * @param[in]   ...
 *     Additional arguments that can be expected by the function, depending
 *     on the message string. Each argument is used to replace an
 *     <em>insert sequence</em> in the message string.
 *
 * @remark
 *     Contrary to printf(), ConPrintf(), ConResPrintf() and associated functions,
 *     the ConMsg* functions work on format strings that contain <em>insert sequences</em>.
 *     These sequences extend the standard <em>format specifiers</em> as they
 *     allow to specify an <em>insert number</em> referring which precise value
 *     given in arguments to use.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintf() and associated functions, ConMsgPrintfV(),
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
__cdecl
ConMsgPrintf(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    ...)
{
    DWORD Len;
    va_list args;

    /* Sanitize dwFlags */
    dwFlags &= ~FORMAT_MESSAGE_ARGUMENT_ARRAY;

    va_start(args, dwLanguageId);
    Len = ConMsgPrintfV(Writer,
                        dwFlags,
                        lpSource,
                        dwMessageId,
                        dwLanguageId,
                        &args);
    va_end(args);

    return Len;
}

/**
 * @name ConResMsgPrintfExV
 *     Formats and writes a message string to a stream. The function requires
 *     a message definition as input. Contrary to the ConMsg* or the Win32
 *     FormatMessage() functions, the message definition comes from a resource
 *     string table, much like the strings for ConResPrintf(), but is formatted
 *     according to the rules of ConMsgPrintf().
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   hInstance
 *     Optional handle to an instance of the module whose executable file
 *     contains the string resource. Can be set to NULL to get the handle
 *     to the application itself.
 *
 * @param[in]   dwFlags
 *     The formatting options, see FormatMessage() for more details.
 *     The only valid flags are @b FORMAT_MESSAGE_ARGUMENT_ARRAY,
 *     @b FORMAT_MESSAGE_IGNORE_INSERTS and @b FORMAT_MESSAGE_MAX_WIDTH_MASK.
 *     All the other flags are internally overridden by the function
 *     to implement its behaviour.
 *
 * @param[in]   uID
 *     The identifier of the message string. The format string follows the
 *     same specifications as the @a lpSource format string in ConMsgPrintf().
 *
 * @param[in]   LanguageId
 *     The language identifier of the resource. If this parameter is
 *     <tt>MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)</tt>, the current language
 *     associated with the calling thread is used. To specify a language other
 *     than the current language, use the @c MAKELANGID macro to create this
 *     parameter.
 *
 * @param[in]   Arguments
 *     Optional pointer to an array of values describing a variable number of
 *     arguments, depending on the message string. Each argument is used to
 *     replace an <em>insert sequence</em> in the message string.
 *     By default, the @p Arguments parameter is of type @c va_list*, initialized
 *     with va_start(). The state of the @c va_list argument is undefined upon
 *     return from the function. To use the @c va_list again, destroy the variable
 *     argument list pointer using va_end() and reinitialize it with va_start().
 *     If you do not have a pointer of type @c va_list*, then specify the
 *     @b FORMAT_MESSAGE_ARGUMENT_ARRAY flag and pass a pointer to an array
 *     of @c DWORD_PTR values; those values are input to the message formatted
 *     as the insert values. Each insert must have a corresponding element in
 *     the array.
 *
 * @remark
 *     Contrary to printf(), ConPrintf(), ConResPrintf() and associated functions,
 *     the ConMsg* functions work on format strings that contain <em>insert sequences</em>.
 *     These sequences extend the standard <em>format specifiers</em> as they
 *     allow to specify an <em>insert number</em> referring which precise value
 *     given in arguments to use.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintf() and associated functions, ConMsgPrintf(),
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
ConResMsgPrintfExV(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN DWORD   dwFlags,
    IN UINT    uID,
    IN LANGID  LanguageId,
    IN va_list *Arguments OPTIONAL)
{
    PWSTR lpMsgBuf = NULL;
    DWORD Len;
    WCHAR bufSrc[CON_RC_STRING_MAX_SIZE];

    /* Retrieve the string from the resource string table */
    // NOTE: We may use the special behaviour where nBufMaxSize == 0
    Len = K32LoadStringExW(hInstance, uID, LanguageId, bufSrc, ARRAYSIZE(bufSrc));
    if (Len == 0)
        return Len;

    /* Sanitize dwFlags */
    dwFlags |= FORMAT_MESSAGE_ALLOCATE_BUFFER; // Always allocate an internal buffer.

    /* The string has already been manually loaded */
    dwFlags &= ~(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM);
    dwFlags |= FORMAT_MESSAGE_FROM_STRING;

    /*
     * Retrieve the message string without appending extra newlines.
     * Use the "safe" FormatMessage version (SEH-protected) to protect
     * from invalid string parameters.
     */
    Len = FormatMessageSafeW(dwFlags,
                             bufSrc,
                             0, 0,
                             (PWSTR)&lpMsgBuf,
                             0,
                             Arguments);

    if (!lpMsgBuf)
    {
        // ASSERT(Len == 0);
    }
    else
    {
        // ASSERT(Len != 0);

        Len *= sizeof(WCHAR);
        if (!CALL_W32(Writer)(lpMsgBuf, Len, &Len))
            Len = 0; /* Fixup returned length in case of errors */
        Len /= sizeof(WCHAR);

        /* Free the buffer allocated by FormatMessage */
        LocalFree(lpMsgBuf);
    }

    return Len;
}

/**
 * @name ConResMsgPrintfV
 *     Formats and writes a message string to a stream. The function requires
 *     a message definition as input. Contrary to the ConMsg* or the Win32
 *     FormatMessage() functions, the message definition comes from a resource
 *     string table, much like the strings for ConResPrintf(), but is formatted
 *     according to the rules of ConMsgPrintf().
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   dwFlags
 *     The formatting options, see FormatMessage() for more details.
 *     The only valid flags are @b FORMAT_MESSAGE_ARGUMENT_ARRAY,
 *     @b FORMAT_MESSAGE_IGNORE_INSERTS and @b FORMAT_MESSAGE_MAX_WIDTH_MASK.
 *     All the other flags are internally overridden by the function
 *     to implement its behaviour.
 *
 * @param[in]   uID
 *     The identifier of the message string. The format string follows the
 *     same specifications as the @a lpSource format string in ConMsgPrintf().
 *
 * @param[in]   Arguments
 *     Optional pointer to an array of values describing a variable number of
 *     arguments, depending on the message string. Each argument is used to
 *     replace an <em>insert sequence</em> in the message string.
 *     By default, the @p Arguments parameter is of type @c va_list*, initialized
 *     with va_start(). The state of the @c va_list argument is undefined upon
 *     return from the function. To use the @c va_list again, destroy the variable
 *     argument list pointer using va_end() and reinitialize it with va_start().
 *     If you do not have a pointer of type @c va_list*, then specify the
 *     @b FORMAT_MESSAGE_ARGUMENT_ARRAY flag and pass a pointer to an array
 *     of @c DWORD_PTR values; those values are input to the message formatted
 *     as the insert values. Each insert must have a corresponding element in
 *     the array.
 *
 * @remark
 *     Contrary to printf(), ConPrintf(), ConResPrintf() and associated functions,
 *     the ConMsg* functions work on format strings that contain <em>insert sequences</em>.
 *     These sequences extend the standard <em>format specifiers</em> as they
 *     allow to specify an <em>insert number</em> referring which precise value
 *     given in arguments to use.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintf() and associated functions, ConMsgPrintf(),
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
ConResMsgPrintfV(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN UINT    uID,
    IN va_list *Arguments OPTIONAL)
{
    return ConResMsgPrintfExV(Writer, NULL /*GetModuleHandleW(NULL)*/,
                              dwFlags, uID,
                              MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                              Arguments);
}

/**
 * @name ConResMsgPrintfEx
 *     Formats and writes a message string to a stream. The function requires
 *     a message definition as input. Contrary to the ConMsg* or the Win32
 *     FormatMessage() functions, the message definition comes from a resource
 *     string table, much like the strings for ConResPrintf(), but is formatted
 *     according to the rules of ConMsgPrintf().
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   hInstance
 *     Optional handle to an instance of the module whose executable file
 *     contains the string resource. Can be set to NULL to get the handle
 *     to the application itself.
 *
 * @param[in]   dwFlags
 *     The formatting options, see FormatMessage() for more details.
 *     The only valid flags are @b FORMAT_MESSAGE_IGNORE_INSERTS and
 *     @b FORMAT_MESSAGE_MAX_WIDTH_MASK. All the other flags are internally
 *     overridden by the function to implement its behaviour.
 *
 * @param[in]   uID
 *     The identifier of the message string. The format string follows the
 *     same specifications as the @a lpSource format string in ConMsgPrintf().
 *
 * @param[in]   LanguageId
 *     The language identifier of the resource. If this parameter is
 *     <tt>MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)</tt>, the current language
 *     associated with the calling thread is used. To specify a language other
 *     than the current language, use the @c MAKELANGID macro to create this
 *     parameter.
 *
 * @param[in]   ...
 *     Additional arguments that can be expected by the function, depending
 *     on the message string. Each argument is used to replace an
 *     <em>insert sequence</em> in the message string.
 *
 * @remark
 *     Contrary to printf(), ConPrintf(), ConResPrintf() and associated functions,
 *     the ConMsg* functions work on format strings that contain <em>insert sequences</em>.
 *     These sequences extend the standard <em>format specifiers</em> as they
 *     allow to specify an <em>insert number</em> referring which precise value
 *     given in arguments to use.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintf() and associated functions, ConMsgPrintf(),
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
__cdecl
ConResMsgPrintfEx(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN DWORD  dwFlags,
    IN UINT   uID,
    IN LANGID LanguageId,
    ...)
{
    DWORD Len;
    va_list args;

    /* Sanitize dwFlags */
    dwFlags &= ~FORMAT_MESSAGE_ARGUMENT_ARRAY;

    va_start(args, LanguageId);
    Len = ConResMsgPrintfExV(Writer,
                             hInstance,
                             dwFlags,
                             uID,
                             LanguageId,
                             &args);
    va_end(args);

    return Len;
}

/**
 * @name ConResMsgPrintf
 *     Formats and writes a message string to a stream. The function requires
 *     a message definition as input. Contrary to the ConMsg* or the Win32
 *     FormatMessage() functions, the message definition comes from a resource
 *     string table, much like the strings for ConResPrintf(), but is formatted
 *     according to the rules of ConMsgPrintf().
 *
 * @param[in]   Stream
 *     Stream to which the write operation is issued.
 *
 * @param[in]   dwFlags
 *     The formatting options, see FormatMessage() for more details.
 *     The only valid flags are @b FORMAT_MESSAGE_IGNORE_INSERTS and
 *     @b FORMAT_MESSAGE_MAX_WIDTH_MASK. All the other flags are internally
 *     overridden by the function to implement its behaviour.
 *
 * @param[in]   uID
 *     The identifier of the message string. The format string follows the
 *     same specifications as the @a lpSource format string in ConMsgPrintf().
 *
 * @param[in]   ...
 *     Additional arguments that can be expected by the function, depending
 *     on the message string. Each argument is used to replace an
 *     <em>insert sequence</em> in the message string.
 *
 * @remark
 *     Contrary to printf(), ConPrintf(), ConResPrintf() and associated functions,
 *     the ConMsg* functions work on format strings that contain <em>insert sequences</em>.
 *     These sequences extend the standard <em>format specifiers</em> as they
 *     allow to specify an <em>insert number</em> referring which precise value
 *     given in arguments to use.
 *
 * @return
 *     Numbers of characters successfully written to @p Stream.
 *
 * @see ConPrintf(), ConResPrintf() and associated functions, ConMsgPrintf(),
 *      <a href="https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage">FormatMessage() (on MSDN)</a>
 **/
DWORD
__cdecl
ConResMsgPrintf(
    IN PCON_WRITER32 Writer,
    IN DWORD dwFlags,
    IN UINT  uID,
    ...)
{
    DWORD Len;
    va_list args;

    /* Sanitize dwFlags */
    dwFlags &= ~FORMAT_MESSAGE_ARGUMENT_ARRAY;

    va_start(args, uID);
    Len = ConResMsgPrintfV(Writer, dwFlags, uID, &args);
    va_end(args);

    return Len;
}

/* EOF */
