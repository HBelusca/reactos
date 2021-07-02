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

#ifndef _UNICODE
#error The ConUtils library at the moment only supports compilation with _UNICODE defined!
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Shadow type, implementation-specific
// typedef struct _CON_STREAM CON_STREAM, *PCON_STREAM;

DWORD
ConStreamWrite(
    IN PCON_STREAM Stream,
    IN PCTCH szStr,
    IN DWORD Len);

VOID
ConClearLine(IN PCON_STREAM Stream);


/*
 * See conprint.h
 */
#ifdef __CONUTILS_OUTSTREAM_FUNCS_COMPAT

// TODO: pragma warning for deprecation.

#define ConWrite(Stream, szStr, len) \
    ConWrite(GET_W32(&(Stream)->Writer), szStr, len)

#define ConPuts(Stream, szStr) \
    ConPuts(GET_W32(&(Stream)->Writer), szStr)

#define ConPrintfV(Stream, szStr, args) \
    ConPrintfV(GET_W32(&(Stream)->Writer), szStr, args)

#define ConPrintf(Stream, szStr, ...) \
    ConPrintf(GET_W32(&(Stream)->Writer), szStr, ##__VA_ARGS__)

#define ConResPutsEx(Stream, hInstance, uID, LanguageId) \
    ConResPutsEx(GET_W32(&(Stream)->Writer), hInstance, uID, LanguageId)

#define ConResPuts(Stream, uID) \
    ConResPuts(GET_W32(&(Stream)->Writer), uID)

#define ConResPrintfExV(Stream, hInstance, uID, LanguageId, args) \
    ConResPrintfExV(GET_W32(&(Stream)->Writer), hInstance, uID, LanguageId, args)

#define ConResPrintfV(Stream, uID, args) \
    ConResPrintfV(GET_W32(&(Stream)->Writer), uID, args)

#define ConResPrintfEx(Stream, hInstance, uID, LanguageId, ...) \
    ConResPrintfEx(GET_W32(&(Stream)->Writer), hInstance, uID, LanguageId, ##__VA_ARGS__)

#define ConResPrintf(Stream, uID, ...) \
    ConResPrintf(GET_W32(&(Stream)->Writer), uID, ##__VA_ARGS__)

#define ConMsgPuts(Stream, dwFlags, lpSource, dwMessageId, dwLanguageId) \
    ConMsgPuts(GET_W32(&(Stream)->Writer), dwFlags, lpSource, dwMessageId, dwLanguageId)

#define ConMsgPrintf2V(Stream, dwFlags, lpSource, dwMessageId, dwLanguageId, args) \
    ConMsgPrintf2V(GET_W32(&(Stream)->Writer), dwFlags, lpSource, dwMessageId, dwLanguageId, args)

#define ConMsgPrintfV(Stream, dwFlags, lpSource, dwMessageId, dwLanguageId, Arguments) \
    ConMsgPrintfV(GET_W32(&(Stream)->Writer), dwFlags, lpSource, dwMessageId, dwLanguageId, Arguments)

#define ConMsgPrintf(Stream, dwFlags, lpSource, dwMessageId, dwLanguageId, ...) \
    ConMsgPrintf(GET_W32(&(Stream)->Writer), dwFlags, lpSource, dwMessageId, dwLanguageId, ##__VA_ARGS__)

#define ConResMsgPrintfExV(Stream, hInstance, dwFlags, uID, LanguageId, Arguments) \
    ConResMsgPrintfExV(Streamv, hInstance, dwFlags, uID, LanguageId, Arguments)

#define ConResMsgPrintfV(Stream, dwFlags, uID, Arguments) \
    ConResMsgPrintfV(GET_W32(&(Stream)->Writer), dwFlags, uID, Arguments)

#define ConResMsgPrintfEx(Stream, hInstance, dwFlags, uID, LanguageId, ...) \
    ConResMsgPrintfEx(GET_W32(&(Stream)->Writer), hInstance, dwFlags, uID, LanguageId, ##__VA_ARGS__)

#define ConResMsgPrintf(Stream, dwFlags, uID, ...) \
    ConResMsgPrintf(GET_W32(&(Stream)->Writer), dwFlags, uID, ##__VA_ARGS__)

#endif // __CONUTILS_OUTSTREAM_FUNCS_COMPAT

#ifdef __cplusplus
}
#endif

#endif  /* __OUTSTREAM_H__ */

/* EOF */
